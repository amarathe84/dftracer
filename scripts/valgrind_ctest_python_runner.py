#!/usr/bin/env python3
"""Run Python-backed CTest tests under valgrind.

This is a companion to ``valgrind_ctest_runner.py``. The C/C++ runner skips
Python interpreter tests because CPython and extension modules need different
Valgrind knobs and often a separate suppression policy. This runner selects
CTest entries that execute Python test code and runs the entire Python process
under Memcheck so leaks in dftracer's native bindings can be caught.

Usage examples:
  python3 scripts/valgrind_ctest_python_runner.py \
      --build-dir build/.../dftracer.dftracer \
      --suppression test/valgrind/test_cpp_known_syscall.supp

  python3 scripts/valgrind_ctest_python_runner.py \
      --build-dir build/.../dftracer.dftracer \
      --tests test_py_both test_py_ai_logging_normal
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable


DEFAULT_EXTERNAL_CONTEXT_IGNORES = [
    r"\bstrncmp\b.*strcmp\.S",
    r"\bis_dst\b.*dl-load\.c",
    r"\b_dl_dst_count\b.*dl-load\.c",
    r"\bexpand_dynamic_string_token\b.*dl-load\.c",
    r"\bfillin_rpath\b.*dl-load\.c",
    r"\bdecompose_rpath\b.*dl-load\.c",
    r"\bcache_rpath\b.*dl-load\.c",
    r"\b_dl_map_object\b.*dl-load\.c",
    r"\bdlopen(?:@@GLIBC|_doit)?\b",
    r"\bdynload_shlib\.c\b",
    r"\bimportdl\.c\b",
    r"\bimport\.c\b",
    r"\b_imp_create_dynamic\b",
    r"\bPyImport_ImportModuleLevelObject\b",
]


def parse_args() -> argparse.Namespace:
    default_jobs = max(1, (os.cpu_count() or 1))
    parser = argparse.ArgumentParser(description="Run Python CTest tests under valgrind")
    parser.add_argument("--build-dir", required=True, help="CTest build directory")
    parser.add_argument(
        "--tests",
        nargs="*",
        default=None,
        help="Specific CTest names to run; omit to run all discovered Python CTests",
    )
    parser.add_argument(
        "--exclude-regex",
        default=None,
        help="Regex of test names to skip after Python-test selection",
    )
    parser.add_argument(
        "--suppression",
        action="append",
        default=[],
        help="Valgrind suppression file. Can be provided multiple times.",
    )
    parser.add_argument(
        "--log-dir",
        default=None,
        help="Directory for valgrind logs (default: <build-dir>/valgrind-python-ctest)",
    )
    parser.add_argument(
        "--summary-json",
        default=None,
        help="Path to write machine-readable summary JSON (default: <log-dir>/summary.json)",
    )
    parser.add_argument(
        "--max-tests",
        type=int,
        default=0,
        help="Run at most N selected tests (0 means no limit)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=900,
        help="Timeout in seconds per test (default: 900)",
    )
    parser.add_argument(
        "--ctest-discovery-timeout",
        type=int,
        default=300,
        help="Timeout in seconds for `ctest --show-only=json-v1` discovery (default: 300)",
    )
    parser.add_argument(
        "--heartbeat-interval",
        type=int,
        default=30,
        help="Print progress every N seconds while a test is running (default: 30, 0 disables)",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        help="DFTRACER_LOG_LEVEL used for valgrind runs (default: INFO)",
    )
    parser.add_argument(
        "--track-leak-kinds",
        default="definite,possible",
        help="Leak kinds that should be displayed and treated as errors.",
    )
    parser.add_argument(
        "--valgrind-track-origins",
        choices=["yes", "no"],
        default="no",
        help="Valgrind track-origins setting. no is much faster in CI.",
    )
    parser.add_argument(
        "--valgrind-num-callers",
        type=int,
        default=20,
        help="Valgrind stack depth for reported contexts.",
    )
    parser.add_argument(
        "--ignore-python-possible-leaks",
        action="store_true",
        help="Only fail on definitely lost leaks/errors; still summarize possible leaks.",
    )
    parser.add_argument(
        "--fail-on-project-frames-only",
        action="store_true",
        help="Only fail when a Valgrind error/leak context contains project frames.",
    )
    parser.add_argument(
        "--fail-on-project-leaks-only",
        action="store_true",
        help="Only fail when a Valgrind leak context contains project frames.",
    )
    parser.add_argument(
        "--project-frame-regex",
        default=r"dftracer|libdftracer|dft_",
        help="Regex used with project-only modes to identify actionable frames.",
    )
    parser.add_argument(
        "--show-external-leak-summary",
        action="store_true",
        help="Print raw whole-process Valgrind leak totals even in project-only modes.",
    )
    parser.add_argument(
        "--ignore-external-context-regex",
        action="append",
        default=[],
        help=(
            "Regex for known external Valgrind contexts to ignore. Defaults include "
            "glibc loader and CPython import/dlopen noise; can be repeated."
        ),
    )
    parser.add_argument(
        "--no-default-external-ignores",
        action="store_true",
        help="Disable built-in ignores for loader/Python import Valgrind noise.",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=default_jobs,
        help="Number of tests to run in parallel (default: all detected cores)",
    )
    return parser.parse_args()


def load_ctest_json(build_dir: Path) -> dict:
    raw = subprocess.check_output(["ctest", "--show-only=json-v1"], cwd=build_dir, text=True)
    return json.loads(raw)


def load_ctest_json_with_timeout(build_dir: Path, timeout: int) -> dict:
    raw = subprocess.check_output(
        ["ctest", "--show-only=json-v1"],
        cwd=build_dir,
        text=True,
        timeout=timeout,
    )
    return json.loads(raw)


def parse_env_items(test_obj: dict) -> list[str]:
    merged: list[str] = []
    for prop in test_obj.get("properties", []):
        if prop.get("name") != "ENVIRONMENT":
            continue
        value = prop.get("value")
        if isinstance(value, list):
            merged.extend([x for x in value if isinstance(x, str) and x])
        elif isinstance(value, str):
            merged.extend([x for x in value.split(";") if x])
    return merged


def test_will_fail(test_obj: dict) -> bool:
    for prop in test_obj.get("properties", []):
        if prop.get("name") != "WILL_FAIL":
            continue
        value = prop.get("value")
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            return value.strip().upper() in {"1", "ON", "TRUE", "YES"}
    return False


def is_python_token(token: str) -> bool:
    name = Path(token).name.lower()
    return name in {"python", "python3"} or name.startswith("python3.") or name.startswith("python.")


def is_python_ctest(test_obj: dict) -> bool:
    cmd = test_obj.get("command") or []
    if not cmd:
        return False

    if is_python_token(cmd[0]):
        return True

    # Some CTests can be launched through wrappers such as mpiexec. Select them
    # if they ultimately invoke Python or a Python source file.
    for token in cmd[1:]:
        if is_python_token(token) or token.endswith(".py"):
            return True
    return False


def selected_tests(all_tests: list[dict], names: list[str] | None, exclude_regex: str | None) -> list[dict]:
    tests_by_name = {t.get("name"): t for t in all_tests if t.get("name")}

    if names:
        missing = [name for name in names if name not in tests_by_name]
        if missing:
            raise ValueError(f"CTest names not found: {', '.join(missing)}")
        chosen = [tests_by_name[name] for name in names]
    else:
        chosen = [t for t in all_tests if t.get("name") and is_python_ctest(t)]

    non_python = [t["name"] for t in chosen if not is_python_ctest(t)]
    if non_python:
        raise ValueError(
            "Requested tests are not Python-backed CTests: " + ", ".join(non_python)
        )

    if exclude_regex:
        rx = re.compile(exclude_regex)
        chosen = [t for t in chosen if not rx.search(t["name"])]

    return chosen


def safe_log_name(test_name: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", test_name)


def parse_count_line(line: str) -> int | None:
    # Example: "==123==    definitely lost: 24 bytes in 1 blocks"
    match = re.search(r":\s+([0-9,]+)\s+bytes\s+in\s+([0-9,]+)\s+blocks", line)
    if not match:
        return None
    return int(match.group(1).replace(",", ""))


def summarize_log(log_file: Path) -> dict:
    summary = {
        "error_summary": "(no ERROR SUMMARY line)",
        "definitely_lost": "(no definitely lost line)",
        "possibly_lost": "(no possibly lost line)",
        "still_reachable": "(no still reachable line)",
        "definitely_lost_bytes": None,
        "possibly_lost_bytes": None,
    }
    if not log_file.exists():
        summary["error_summary"] = "(missing valgrind log)"
        return summary

    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if "ERROR SUMMARY:" in line and summary["error_summary"].startswith("(no"):
                summary["error_summary"] = line
            elif "definitely lost:" in line and summary["definitely_lost"].startswith("(no"):
                summary["definitely_lost"] = line
                summary["definitely_lost_bytes"] = parse_count_line(line)
            elif "possibly lost:" in line and summary["possibly_lost"].startswith("(no"):
                summary["possibly_lost"] = line
                summary["possibly_lost_bytes"] = parse_count_line(line)
            elif "still reachable:" in line and summary["still_reachable"].startswith("(no"):
                summary["still_reachable"] = line
    return summary


def extract_error_excerpt(log_file: Path, max_lines: int = 100) -> str:
    if not log_file.exists():
        return "(missing valgrind log)"

    tokens = (
        "Invalid",
        "Use of uninitialised",
        "Uninitialised",
        "Conditional jump",
        "Syscall param",
        "Address 0x",
        "Mismatched",
        "Invalid free",
        "definitely lost:",
        "possibly lost:",
        "still reachable:",
        "ERROR SUMMARY:",
        "    at 0x",
        "    by 0x",
    )
    keep: list[str] = []
    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if line.startswith("==") and any(token in line for token in tokens):
                keep.append(line)
            if len(keep) >= max_lines:
                break
    return "\n".join(keep) if keep else "(no parsed error excerpt)"


def extract_contexts(log_file: Path) -> list[list[str]]:
    if not log_file.exists():
        return []

    contexts: list[list[str]] = []
    current: list[str] = []
    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\n")
            if not line.startswith("=="):
                continue
            body = re.sub(r"^==\d+==\s?", "", line)
            starts_context = (
                body.startswith("Invalid ")
                or body.startswith("Use of uninitialised")
                or body.startswith("Uninitialised")
                or body.startswith("Conditional jump")
                or body.startswith("Syscall param")
                or body.startswith("Mismatched")
                or body.startswith("Invalid free")
                or re.match(r"^[0-9,]+ bytes in [0-9,]+ blocks are ", body) is not None
            )
            if starts_context and current:
                contexts.append(current)
                current = []
            if starts_context or current:
                current.append(line)
            elif body == "" and current:
                contexts.append(current)
                current = []
    if current:
        contexts.append(current)
    return contexts


def context_matches(context: list[str], patterns: Iterable[re.Pattern[str]]) -> bool:
    text = "\n".join(context)
    return any(pattern.search(text) for pattern in patterns)


def actionable_contexts(
    log_file: Path,
    ignore_patterns: Iterable[re.Pattern[str]],
    project_regex: str | None = None,
) -> tuple[list[list[str]], int]:
    project_rx = re.compile(project_regex) if project_regex else None
    actionable: list[list[str]] = []
    ignored_count = 0
    for context in extract_contexts(log_file):
        has_project_frame = (
            project_rx is not None and any(project_rx.search(line) for line in context)
        )
        if not has_project_frame and context_matches(context, ignore_patterns):
            ignored_count += 1
            continue
        actionable.append(context)
    return actionable, ignored_count


def extract_project_excerpt(log_file: Path, project_regex: str, max_contexts: int = 3) -> tuple[int, str]:
    rx = re.compile(project_regex)
    matching: list[list[str]] = []
    for context in extract_contexts(log_file):
        if any(rx.search(line) for line in context):
            matching.append(context)

    lines: list[str] = []
    for context in matching[:max_contexts]:
        if lines:
            lines.append("...")
        lines.extend(context[:30])
    return len(matching), "\n".join(lines) if lines else "(no project-frame contexts)"


def extract_actionable_excerpt(
    contexts: list[list[str]], max_contexts: int = 3
) -> tuple[int, str]:
    lines: list[str] = []
    for context in contexts[:max_contexts]:
        if lines:
            lines.append("...")
        lines.extend(context[:30])
    return len(contexts), "\n".join(lines) if lines else "(no actionable contexts)"


def is_leak_context(context: list[str]) -> bool:
    if not context:
        return False
    body = re.sub(r"^==\d+==\s?", "", context[0])
    return re.match(
        r"^[0-9,]+ bytes in [0-9,]+ blocks are (definitely|indirectly|possibly) lost",
        body,
    ) is not None


def extract_project_leak_excerpt(log_file: Path, project_regex: str, max_contexts: int = 3) -> tuple[int, str]:
    rx = re.compile(project_regex)
    matching: list[list[str]] = []
    for context in extract_contexts(log_file):
        if is_leak_context(context) and any(rx.search(line) for line in context):
            matching.append(context)

    lines: list[str] = []
    for context in matching[:max_contexts]:
        if lines:
            lines.append("...")
        lines.extend(context[:30])
    return len(matching), "\n".join(lines) if lines else "(no project-leak contexts)"


def build_env(test_obj: dict, log_level: str) -> dict[str, str]:
    env = os.environ.copy()
    for kv in parse_env_items(test_obj):
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v

    env["DFTRACER_LOG_LEVEL"] = log_level
    env["DFTRACER_BIND_SIGNALS"] = "0"

    # CPython's small-object allocator can hide useful native allocation
    # context. The malloc allocator gives Memcheck clearer ownership stacks.
    env.setdefault("PYTHONMALLOC", "malloc")
    env.setdefault("PYTHONDONTWRITEBYTECODE", "1")
    return env


def run_under_valgrind(
    build_dir: Path,
    log_dir: Path,
    test_obj: dict,
    suppression_files: Iterable[Path],
    log_level: str,
    timeout: int,
    heartbeat_interval: int,
    track_leak_kinds: str,
    errors_for_leak_kinds: str,
    track_origins: str,
    num_callers: int,
    show_external_leak_summary: bool,
) -> tuple[int, Path, Path, dict, str]:
    test_name = test_obj["name"]
    cmd = test_obj.get("command") or []
    if not cmd:
        raise RuntimeError(f"CTest '{test_name}' has empty command")

    log_base = safe_log_name(test_name)
    valgrind_log = log_dir / f"{log_base}.valgrind.log"
    command_log = log_dir / f"{log_base}.command.log"

    vg_cmd = [
        "valgrind",
        "--tool=memcheck",
        "--leak-check=full",
        f"--show-leak-kinds={track_leak_kinds}",
        f"--errors-for-leak-kinds={errors_for_leak_kinds}",
        f"--track-origins={track_origins}",
        f"--num-callers={num_callers}",
        "--error-exitcode=99",
        f"--log-file={valgrind_log}",
    ]
    for suppression in suppression_files:
        vg_cmd.append(f"--suppressions={suppression}")
    vg_cmd.extend(cmd)

    env = build_env(test_obj, log_level)
    printable_cmd = " ".join(shlex.quote(x) for x in cmd)
    printable_vg_cmd = " ".join(shlex.quote(x) for x in vg_cmd)

    print(f"[valgrind-python-ctest] RUN {test_name}: {printable_cmd}")
    with command_log.open("w", encoding="utf-8", errors="replace") as out:
        out.write(f"$ {printable_vg_cmd}\n\n")
        out.flush()
        start = time.monotonic()
        next_heartbeat = start + heartbeat_interval if heartbeat_interval > 0 else None
        proc = subprocess.Popen(
            vg_cmd,
            cwd=build_dir,
            env=env,
            text=True,
            stdout=out,
            stderr=subprocess.STDOUT,
        )
        while proc.poll() is None:
            elapsed = time.monotonic() - start
            if timeout > 0 and elapsed > timeout:
                proc.kill()
                proc.wait()
                out.write(f"\n[valgrind-python-ctest] timeout after {timeout} seconds\n")
                out.flush()
                rc = 124
                break
            if next_heartbeat is not None and time.monotonic() >= next_heartbeat:
                print(
                    f"[valgrind-python-ctest] {test_name} still running after {int(elapsed)}s "
                    f"(command log: {command_log}, valgrind log: {valgrind_log})",
                    flush=True,
                )
                next_heartbeat += heartbeat_interval
            time.sleep(0.2)
        else:
            rc = proc.returncode

        if rc == 124:
            print(
                f"[valgrind-python-ctest] {test_name} timed out after {timeout}s "
                f"(command log: {command_log}, valgrind log: {valgrind_log})",
                flush=True,
            )

    summary = summarize_log(valgrind_log)
    excerpt = extract_error_excerpt(valgrind_log)
    print(f"[valgrind-python-ctest] {test_name} rc={rc}")
    if show_external_leak_summary:
        print(f"[valgrind-python-ctest] {test_name} {summary['error_summary']}")
        print(f"[valgrind-python-ctest] {test_name} {summary['definitely_lost']}")
        print(f"[valgrind-python-ctest] {test_name} {summary['possibly_lost']}")

    return rc, valgrind_log, command_log, summary, excerpt


def write_summary(summary_path: Path, payload: dict) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)


def print_clean_summary(payload: dict) -> None:
    print("[valgrind-python-ctest] ===== Summary =====")
    print(f"[valgrind-python-ctest] selected_tests={payload['selected_tests']}")
    print(f"[valgrind-python-ctest] valgrind_executed={payload['valgrind_executed']}")
    print(f"[valgrind-python-ctest] failures={payload['failures']}")
    if payload["failed_tests"]:
        print("[valgrind-python-ctest] failing_tests:")
        for item in payload["failed_tests"]:
            print(f"  - {item['name']}: {item['error_summary']}")
            print(f"    {item['definitely_lost']}")
            print(f"    {item['possibly_lost']}")


def run_single_python_test(
    build_dir: Path,
    log_dir: Path,
    test_obj: dict,
    suppression_files: list[Path],
    args: argparse.Namespace,
    show_external_leak_summary: bool,
    ignore_context_patterns: list[re.Pattern[str]],
) -> dict | None:
    expect_fail = test_will_fail(test_obj)
    try:
        rc, log_file, command_log, summary, excerpt = run_under_valgrind(
            build_dir=build_dir,
            log_dir=log_dir,
            test_obj=test_obj,
            suppression_files=suppression_files,
            log_level=args.log_level,
            timeout=args.timeout,
            heartbeat_interval=args.heartbeat_interval,
            track_leak_kinds=args.track_leak_kinds,
            errors_for_leak_kinds=(
                "definite" if args.ignore_python_possible_leaks else args.track_leak_kinds
            ),
            track_origins=args.valgrind_track_origins,
            num_callers=args.valgrind_num_callers,
            show_external_leak_summary=show_external_leak_summary,
        )
    except Exception as exc:
        return {
            "name": test_obj.get("name", "<unknown>"),
            "log_file": "(runner exception)",
            "command_log": "(runner exception)",
            "returncode": 127,
            "error_summary": str(exc),
            "definitely_lost": "(runner exception)",
            "possibly_lost": "(runner exception)",
            "still_reachable": "(runner exception)",
            "error_excerpt": "(runner exception)",
        }

    has_errors = "ERROR SUMMARY: 0 errors from 0 contexts" not in summary["error_summary"]
    actionable, ignored_context_count = actionable_contexts(
        log_file, ignore_context_patterns, args.project_frame_regex
    )
    actionable_context_count, actionable_excerpt = extract_actionable_excerpt(actionable)
    project_context_count, project_excerpt = extract_project_excerpt(
        log_file, args.project_frame_regex
    )
    project_leak_context_count, project_leak_excerpt = extract_project_leak_excerpt(
        log_file, args.project_frame_regex
    )

    if expect_fail:
        if args.fail_on_project_leaks_only:
            failed = project_leak_context_count > 0
        elif args.fail_on_project_frames_only:
            failed = project_context_count > 0
        else:
            failed = has_errors and actionable_context_count > 0
    else:
        if args.fail_on_project_leaks_only:
            failed = rc not in (0, 99, 124) or project_leak_context_count > 0
        elif args.fail_on_project_frames_only:
            failed = rc not in (0, 99) or project_context_count > 0
        else:
            failed = (rc != 0 and rc != 99) or (has_errors and actionable_context_count > 0)

    if has_errors and project_leak_context_count == 0 and args.fail_on_project_leaks_only:
        print(
            f"[valgrind-python-ctest] {test_obj['name']} dftracer leak contexts=0 "
            f"(ignored external Valgrind noise; log: {log_file})"
        )
    elif project_leak_context_count > 0:
        print(
            f"[valgrind-python-ctest] {test_obj['name']} "
            f"dftracer leak contexts={project_leak_context_count}"
        )
    elif has_errors and project_context_count == 0 and args.fail_on_project_frames_only:
        print(
            f"[valgrind-python-ctest] {test_obj['name']} has Valgrind noise, "
            "but no project-frame contexts"
        )
    elif has_errors and ignored_context_count > 0 and actionable_context_count == 0:
        print(
            f"[valgrind-python-ctest] {test_obj['name']} ignored "
            f"{ignored_context_count} known external Valgrind contexts"
        )
    elif project_context_count > 0:
        print(
            f"[valgrind-python-ctest] {test_obj['name']} project-frame contexts={project_context_count}"
        )

    if not failed:
        return None

    return {
        "name": test_obj["name"],
        "log_file": str(log_file),
        "command_log": str(command_log),
        "returncode": rc,
        "error_summary": summary["error_summary"],
        "definitely_lost": summary["definitely_lost"],
        "possibly_lost": summary["possibly_lost"],
        "still_reachable": summary["still_reachable"],
        "definitely_lost_bytes": summary["definitely_lost_bytes"],
        "possibly_lost_bytes": summary["possibly_lost_bytes"],
        "error_excerpt": excerpt,
        "project_context_count": project_context_count,
        "project_error_excerpt": project_excerpt,
        "project_leak_context_count": project_leak_context_count,
        "project_leak_excerpt": project_leak_excerpt,
        "ignored_external_context_count": ignored_context_count,
        "actionable_context_count": actionable_context_count,
        "actionable_error_excerpt": actionable_excerpt,
    }


def main() -> int:
    args = parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(line_buffering=True)

    build_dir = Path(args.build_dir).resolve()
    if not build_dir.is_dir():
        print(f"[valgrind-python-ctest] build dir not found: {build_dir}")
        return 2

    suppression_files = [Path(p).resolve() for p in args.suppression]
    for suppression in suppression_files:
        if not suppression.is_file():
            print(f"[valgrind-python-ctest] suppression file not found: {suppression}")
            return 2

    log_dir = (
        Path(args.log_dir).resolve()
        if args.log_dir
        else build_dir / "valgrind-python-ctest"
    )
    log_dir.mkdir(parents=True, exist_ok=True)
    summary_json = (
        Path(args.summary_json).resolve()
        if args.summary_json
        else log_dir / "summary.json"
    )

    try:
        print(
            "[valgrind-python-ctest] discovering CTests via "
            f"`ctest --show-only=json-v1` (timeout={args.ctest_discovery_timeout}s)"
        )
        discovery_start = time.monotonic()
        ctest_info = load_ctest_json_with_timeout(build_dir, args.ctest_discovery_timeout)
        discovery_elapsed = int(time.monotonic() - discovery_start)
        print(
            "[valgrind-python-ctest] CTest discovery complete "
            f"in {discovery_elapsed}s"
        )
        tests = selected_tests(ctest_info.get("tests", []), args.tests, args.exclude_regex)
    except Exception as exc:
        print(f"[valgrind-python-ctest] {exc}")
        return 2

    if args.max_tests and args.max_tests > 0:
        tests = tests[: args.max_tests]

    if args.jobs < 1:
        print("[valgrind-python-ctest] --jobs must be >= 1")
        return 2

    if not tests:
        print("[valgrind-python-ctest] no Python CTest tests selected")
        payload = {
            "selected_tests": 0,
            "valgrind_executed": 0,
            "failures": 0,
            "failed_tests": [],
        }
        write_summary(summary_json, payload)
        return 0

    if shutil.which("valgrind") is None:
        print("[valgrind-python-ctest] valgrind not found in PATH")
        payload = {
            "selected_tests": len(tests),
            "valgrind_executed": 0,
            "failures": 1,
            "failed_tests": [
                {
                    "name": "__valgrind__",
                    "log_file": "(runner)",
                    "command_log": "(runner)",
                    "error_summary": "valgrind not found in PATH",
                    "definitely_lost": "install valgrind and retry",
                    "possibly_lost": "install valgrind and retry",
                }
            ],
        }
        write_summary(summary_json, payload)
        print_clean_summary(payload)
        return 1

    errors_for_leak_kinds = "definite" if args.ignore_python_possible_leaks else args.track_leak_kinds
    print(f"[valgrind-python-ctest] selected {len(tests)} Python CTests")
    print(f"[valgrind-python-ctest] errors-for-leak-kinds={errors_for_leak_kinds}")
    print(
        "[valgrind-python-ctest] valgrind knobs: "
        f"track-origins={args.valgrind_track_origins}, "
        f"num-callers={args.valgrind_num_callers}"
    )
    if args.fail_on_project_leaks_only:
        args.fail_on_project_frames_only = False
        print(f"[valgrind-python-ctest] failing only on project leak frames matching: {args.project_frame_regex}")
    if args.fail_on_project_frames_only:
        print(f"[valgrind-python-ctest] failing only on project frames matching: {args.project_frame_regex}")
    ignore_context_regexes = list(args.ignore_external_context_regex)
    if not args.no_default_external_ignores:
        ignore_context_regexes = [*DEFAULT_EXTERNAL_CONTEXT_IGNORES, *ignore_context_regexes]
    ignore_context_patterns = [re.compile(pattern) for pattern in ignore_context_regexes]
    if ignore_context_regexes:
        print("[valgrind-python-ctest] ignoring known external contexts:")
        for pattern in ignore_context_regexes:
            print(f"[valgrind-python-ctest]   {pattern}")
    show_external_leak_summary = (
        args.show_external_leak_summary
        or not args.fail_on_project_leaks_only
    )

    failing: list[dict] = []
    worker_count = min(args.jobs, max(1, len(tests)))
    if len(tests) > 1 and worker_count > 1:
        print(f"[valgrind-python-ctest] running tests in parallel with {worker_count} workers")

    if worker_count == 1:
        for test_obj in tests:
            failure = run_single_python_test(
                build_dir=build_dir,
                log_dir=log_dir,
                test_obj=test_obj,
                suppression_files=suppression_files,
                args=args,
                show_external_leak_summary=show_external_leak_summary,
                ignore_context_patterns=ignore_context_patterns,
            )
            if failure is not None:
                failing.append(failure)
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=worker_count) as executor:
            futures = [
                executor.submit(
                    run_single_python_test,
                    build_dir,
                    log_dir,
                    test_obj,
                    suppression_files,
                    args,
                    show_external_leak_summary,
                    ignore_context_patterns,
                )
                for test_obj in tests
            ]
            for future in concurrent.futures.as_completed(futures):
                failure = future.result()
                if failure is not None:
                    failing.append(failure)

    failing.sort(key=lambda item: item["name"])

    payload = {
        "selected_tests": len(tests),
        "valgrind_executed": len(tests),
        "failures": len(failing),
        "failed_tests": failing,
    }
    write_summary(summary_json, payload)
    print_clean_summary(payload)

    if failing:
        print("[valgrind-python-ctest] FAIL: Python CTest valgrind errors detected")
        for item in failing:
            print(f"  - {item['name']}: {item['log_file']}")
            print(f"    command_log: {item['command_log']}")
            print(f"    {item['error_summary']}")
            print(f"    {item['definitely_lost']}")
            print(f"    {item['possibly_lost']}")
            project_leak_excerpt = item.get("project_leak_excerpt")
            if project_leak_excerpt and project_leak_excerpt != "(no project-leak contexts)":
                print("    --- dftracer leak excerpt ---")
                for line in project_leak_excerpt.splitlines()[:40]:
                    print(f"    {line}")
            project_excerpt = item.get("project_error_excerpt")
            if project_excerpt and project_excerpt != "(no project-frame contexts)":
                print("    --- project-frame excerpt ---")
                for line in project_excerpt.splitlines()[:40]:
                    print(f"    {line}")
            actionable_excerpt = item.get("actionable_error_excerpt")
            if actionable_excerpt and actionable_excerpt != "(no actionable contexts)":
                print("    --- actionable excerpt ---")
                for line in actionable_excerpt.splitlines()[:40]:
                    print(f"    {line}")
            excerpt = item.get("error_excerpt")
            if excerpt and excerpt != "(no parsed error excerpt)":
                print("    --- excerpt ---")
                for line in excerpt.splitlines()[:40]:
                    print(f"    {line}")
        return 1

    print("[valgrind-python-ctest] PASS: all selected Python CTests clean under valgrind")
    return 0


if __name__ == "__main__":
    sys.exit(main())
