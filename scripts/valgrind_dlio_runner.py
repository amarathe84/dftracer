#!/usr/bin/env python3
"""Run DLIO workload phases under valgrind and rerun failures with gdb."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import traceback
from pathlib import Path
from typing import Iterable

import yaml


DEFAULT_EXTERNAL_CONTEXT_IGNORES = [
    r"\bstrncmp\b.*strcmp\.S",
    r"\bis_dst\b.*dl-load\.c",
    r"\b_dl_dst_count\b.*dl-load\.c",
    r"\bexpand_dynamic_string_token\b.*dl-load\.c",
    r"\bfillin_rpath\b.*dl-load\.c",
    r"\bdecompose_rpath\b.*dl-load\.c",
    r"\bcache_rpath\b.*dl-load\.c",
    r"\b_dl_map_object\b.*dl-load\.c",
    r"\ballocate_dtv_entry\b.*dl-tls\.c",
    r"\ballocate_and_init\b.*dl-tls\.c",
    r"\btls_get_addr_tail\b.*dl-tls\.c",
    r"\b__tls_get_addr\b.*tls_get_addr\.S",
    r"\brtld-malloc\.h\b",
    r"\bdl-tls\.c\b",
    r"\boptree/._C\b",
    r"\boptree/_C\b",
    r"\boptree\b.*\b__tls_get_addr\b",
    r"\boptree\b.*\ballocate_dtv_entry\b",
    r"\bdlopen(?:@@GLIBC|_doit)?\b",
    r"\bdynload_shlib\.c\b",
    r"\bimportdl\.c\b",
    r"\bimport\.c\b",
    r"\b_imp_create_dynamic\b",
    r"\bPyImport_ImportModuleLevelObject\b",
    r"\bsite-packages\b",
    r"\bdist-packages\b",
    r"/python3\.[0-9]+/",
    r"\blibpython3\.[0-9]+\b",
    r"\boptree\b",
    r"\bnumpy\b",
    r"\bscipy\b",
    r"\bpandas\b",
    r"\bmatplotlib\b",
    r"\bh5py\b",
    r"\bmpi4py\b",
    r"\bjaxlib\b",
    r"\btorch\b",
    r"\btensorflow\b",
    r"\bjax\b",
    r"\bcupy\b",
    r"\bpyarrow\b",
    # CPython stdlib: os.environ mutations via setenv/tsearch are not dftracer leaks.
    r"\bos_putenv\b.*posixmodule",
    r"\b__add_to_environ\b.*setenv\.c",
    r"\btsearch\b.*tsearch\.c",
    r"\bsetenv\b.*vgpreload_memcheck",
]


def log(message: str) -> None:
    # Force flush so progress is visible even when stdout is redirected.
    print(message, flush=True)


def as_text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run DLIO workloads under valgrind")
    parser.add_argument(
        "--configs-dir",
        default=None,
        help="DLIO workload config directory. Defaults to the installed dlio_benchmark configs.",
    )
    parser.add_argument(
        "--exclude-workload",
        action="append",
        default=[],
        help="Workload config name to skip, without .yaml. Can be repeated.",
    )
    parser.add_argument(
        "--workload",
        action="append",
        default=[],
        help="Only run this workload config name, without .yaml. Can be repeated.",
    )
    parser.add_argument(
        "--log-dir",
        required=True,
        help="Directory for valgrind, command, and gdb logs.",
    )
    parser.add_argument(
        "--run-root",
        required=True,
        help="Directory used for generated DLIO data and output.",
    )
    parser.add_argument(
        "--phase",
        choices=["generate", "train", "checkpoint", "both"],
        default="both",
        help="Which DLIO phase(s) to execute.",
    )
    parser.add_argument(
        "--clean-run-root",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Whether to delete run-root before executing tests.",
    )
    parser.add_argument(
        "--summary-json",
        required=True,
        help="Path to write machine-readable summary JSON.",
    )
    parser.add_argument(
        "--suppression",
        action="append",
        default=[],
        help="Valgrind suppression file. Can be repeated.",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=600,
        help="Timeout in seconds for each valgrind/gdb phase.",
    )
    parser.add_argument(
        "--gdb-on-timeout",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="Whether to rerun failing tests under gdb when the failure is only a timeout.",
    )
    parser.add_argument(
        "--valgrind-track-origins",
        choices=["yes", "no"],
        default="yes",
        help="Valgrind track-origins setting. no is much faster in CI.",
    )
    parser.add_argument(
        "--valgrind-num-callers",
        type=int,
        default=40,
        help="Valgrind stack depth for reported contexts.",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        help="DFTRACER_LOG_LEVEL for valgrind runs.",
    )
    parser.add_argument(
        "--dftracer-enable",
        choices=["0", "1"],
        default="1",
        help="Whether to enable DFTRACER during runs.",
    )
    parser.add_argument(
        "--gdb-log-level",
        default="DEBUG",
        help="DFTRACER_LOG_LEVEL for gdb reruns.",
    )
    parser.add_argument(
        "--focus-dftracer-leaks-only",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "Only fail when a Valgrind leak context contains dftracer/project frames "
            "(default: enabled). Use --no-focus-dftracer-leaks-only to fail on broader "
            "actionable Valgrind errors."
        ),
    )
    parser.add_argument(
        "--fail-on-project-leaks-only",
        action="store_true",
        help="Deprecated alias for --focus-dftracer-leaks-only.",
    )
    parser.add_argument(
        "--project-frame-regex",
        default=r"(/dftracer/(src|include|python|scripts|test)/|libdftracer|dft_)",
        help=(
            "Regex used with --focus-dftracer-leaks-only to identify actionable frames. "
            "Defaults to dftracer source/build paths rather than any path containing the repo name."
        ),
    )
    parser.add_argument(
        "--show-external-leak-summary",
        action="store_true",
        help="Print raw whole-process Valgrind leak totals in project-only mode.",
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
        "--inline-error-context-lines",
        type=int,
        default=30,
        help="How many lines of actionable valgrind context to print inline for failing tests.",
    )
    parser.add_argument(
        "--inline-command-output-lines",
        type=int,
        default=80,
        help="How many DLIO stdout/stderr lines to print inline from each command log.",
    )
    parser.add_argument(
        "--inline-gdb-output-lines",
        type=int,
        default=80,
        help="How many gdb output lines to print inline when a rerun is triggered.",
    )
    return parser.parse_args()


def resolve_configs_dir(explicit: str | None) -> tuple[Path, Path]:
    if explicit:
        path = Path(explicit).resolve()
        if not path.is_dir():
            raise RuntimeError(f"DLIO configs dir not found: {path}")
        return path, path.parent.parent

    spec = importlib.util.find_spec("dlio_benchmark")
    if spec is None or spec.submodule_search_locations is None:
        raise RuntimeError("Could not resolve dlio_benchmark package")

    package_dir = Path(next(iter(spec.submodule_search_locations))).resolve()
    candidates = [
        package_dir / "configs" / "workload",
        package_dir / "dlio_benchmark" / "configs" / "workload",
    ]
    configs_dir = next((path for path in candidates if path.is_dir()), None)
    if configs_dir is None:
        raise RuntimeError(
            "Could not resolve DLIO workload config directory. Checked: "
            + ", ".join(str(path) for path in candidates)
        )
    return configs_dir, package_dir


def discover_workloads(configs_dir: Path, include: list[str], exclude: list[str]) -> list[str]:
    available = sorted(path.stem for path in configs_dir.glob("*.yaml"))
    available_set = set(available)

    if include:
        missing = sorted(set(include) - available_set)
        if missing:
            raise RuntimeError(f"Requested DLIO workloads were not found: {', '.join(missing)}")
        selected = sorted(include)
    else:
        selected = available

    excluded = set(exclude)
    return [workload for workload in selected if workload not in excluded]


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def selected_phases(phase: str) -> list[str]:
    if phase == "both":
        return ["generate", "train"]
    return [phase]


def parse_workflow_flags(config_path: Path) -> dict[str, bool]:
    if not config_path.is_file():
        return {}

    data = yaml.safe_load(config_path.read_text(encoding="utf-8", errors="replace")) or {}
    workflow = data.get("workflow") if isinstance(data, dict) else None
    if not isinstance(workflow, dict):
        return {}

    flags: dict[str, bool] = {}
    for key in ("train", "generate_data", "checkpoint"):
        value = workflow.get(key)
        if isinstance(value, bool):
            flags[key] = value
    return flags


def phase_skip_reason(configs_dir: Path, workload: str, phase: str) -> str | None:
    flags = parse_workflow_flags(configs_dir / f"{workload}.yaml")
    if not flags:
        return None

    train_enabled = flags.get("train")
    generate_enabled = flags.get("generate_data")
    checkpoint_enabled = flags.get("checkpoint")

    if phase == "train" and train_enabled is False:
        return "workflow.train=False"

    if phase == "checkpoint" and checkpoint_enabled is False:
        return "workflow.checkpoint=False"

    if (
        phase == "generate"
        and generate_enabled is False
        and train_enabled is False
        and checkpoint_enabled is True
    ):
        return "checkpoint-only workload"

    return None


def verify_dftracer_available() -> None:
    try:
        import dftracer.dftracer as native_dftracer  # type: ignore

        module_path = getattr(native_dftracer, "__file__", "(unknown)")
        log(f"[valgrind-dlio] DFTracer native module loaded: {module_path}")
    except Exception:  # pragma: no cover
        log("[valgrind-dlio] ERROR: DFTRACER_ENABLE=1 but dftracer native module failed to import")
        traceback.print_exc()
        raise SystemExit(2)


def summarize_log(log_file: Path) -> tuple[str, str]:
    if not log_file.exists():
        return "(missing valgrind log)", "(no leak line)"

    error_summary = "(no ERROR SUMMARY line)"
    leak_line = "(no definitely lost line)"
    with log_file.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "ERROR SUMMARY:" in line and error_summary.startswith("(no"):
                error_summary = line.strip()
            if "definitely lost:" in line and leak_line.startswith("(no"):
                leak_line = line.strip()
            if not error_summary.startswith("(no") and not leak_line.startswith("(no"):
                break
    return error_summary, leak_line


def print_command_log_excerpt(command_log: Path, label: str, max_lines: int = 80) -> None:
    if not command_log.exists():
        log(f"[valgrind-dlio] {label} command output missing: {command_log}")
        return

    lines = command_log.read_text(encoding="utf-8", errors="replace").splitlines()
    body = lines
    if lines and lines[0].startswith("$ "):
        body = lines[2:] if len(lines) >= 2 and lines[1] == "" else lines[1:]

    if not body:
        log(f"[valgrind-dlio] {label} command output: (empty)")
        return

    snippet = body[-max_lines:]
    log(
        f"[valgrind-dlio] --- command output ({label}, last {len(snippet)} lines) ---"
    )
    for line in snippet:
        log(f"[valgrind-dlio] {line}")


def print_gdb_log_excerpt(gdb_log: Path, label: str, max_lines: int = 80) -> None:
    if not gdb_log.exists():
        log(f"[valgrind-dlio] {label} gdb output missing: {gdb_log}")
        return

    lines = gdb_log.read_text(encoding="utf-8", errors="replace").splitlines()
    body = lines
    if lines and lines[0].startswith("$ "):
        body = lines[2:] if len(lines) >= 2 and lines[1] == "" else lines[1:]

    if not body:
        log(f"[valgrind-dlio] {label} gdb output: (empty)")
        return

    snippet = body[-max_lines:]
    log(
        f"[valgrind-dlio] --- gdb output ({label}, last {len(snippet)} lines) ---"
    )
    for line in snippet:
        log(f"[valgrind-dlio] {line}")


def extract_error_excerpt(log_file: Path, max_lines: int = 100) -> str:
    if not log_file.exists():
        return "(missing valgrind log)"

    keep: list[str] = []
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


def filter_context_lines(
    context: list[str],
    ignore_patterns: Iterable[re.Pattern[str]],
) -> list[str]:
    filtered: list[str] = []
    for line in context:
        if any(pattern.search(line) for pattern in ignore_patterns):
            continue
        filtered.append(line)
    return filtered


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


def is_leak_context(context: list[str]) -> bool:
    if not context:
        return False
    body = re.sub(r"^==\d+==\s?", "", context[0])
    return re.match(
        r"^[0-9,]+ bytes in [0-9,]+ blocks are (definitely|indirectly|possibly) lost",
        body,
    ) is not None


def extract_project_leak_excerpt(
    log_file: Path,
    project_regex: str,
    ignore_patterns: Iterable[re.Pattern[str]],
    max_contexts: int = 3,
) -> tuple[int, str]:
    rx = re.compile(project_regex)
    matching: list[list[str]] = []
    for context in extract_contexts(log_file):
        if is_leak_context(context) and any(rx.search(line) for line in context):
            matching.append(context)

    lines: list[str] = []
    for context in matching[:max_contexts]:
        filtered = filter_context_lines(context, ignore_patterns)
        if not filtered:
            continue
        if lines:
            lines.append("...")
        lines.extend(filtered[:30])
    return len(matching), "\n".join(lines) if lines else "(no project-leak contexts)"


def extract_actionable_excerpt(
    contexts: list[list[str]],
    ignore_patterns: Iterable[re.Pattern[str]],
    max_contexts: int = 3,
) -> tuple[int, str]:
    lines: list[str] = []
    for context in contexts[:max_contexts]:
        filtered = filter_context_lines(context, ignore_patterns)
        if not filtered:
            continue
        if lines:
            lines.append("...")
        lines.extend(filtered[:30])
    return len(contexts), "\n".join(lines) if lines else "(no actionable contexts)"


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", errors="replace")


def build_overrides(workload: str, phase: str, data_root: Path, output_root: Path) -> list[str]:
    workload_data = data_root / workload / "dataset"
    workload_checkpoint = data_root / workload / "checkpoint"
    workload_output = output_root / workload / phase

    common_overrides = [
        "++workload.dataset.num_files_train=16",
        "++workload.dataset.num_files_eval=4",
        "++workload.dataset.num_samples_per_file=1",
        "++workload.dataset.record_length_bytes=4096",
        "++workload.dataset.record_length_bytes_stdev=0",
        "++workload.dataset.record_length_bytes_resize=4096",
        "++workload.reader.read_threads=2",
        "++workload.reader.batch_size=1",
        "++workload.reader.batch_size_eval=1",
        "++workload.train.epochs=1",
        "++workload.train.total_training_steps=1",
        "++workload.workflow.evaluation=False",
        "++workload.workflow.checkpoint=False",
        "++workload.workflow.profiling=False",
        "++workload.storage.storage_type=local_fs",
        f"++workload.storage.storage_root={data_root}",
    ]

    phase_overrides = {
        "generate": [
            "++workload.workflow.generate_data=True",
            "++workload.workflow.train=False",
            "++workload.workflow.checkpoint=False",
        ],
        "train": [
            "++workload.workflow.generate_data=False",
            "++workload.workflow.train=True",
            "++workload.workflow.checkpoint=False",
        ],
        "checkpoint": [
            "++workload.workflow.generate_data=False",
            "++workload.workflow.train=False",
            "++workload.workflow.checkpoint=True",
        ],
    }[phase]

    return [
        f"workload={workload}",
        f"++workload.dataset.data_folder={workload_data}",
        f"++workload.checkpoint.checkpoint_folder={workload_checkpoint}",
        *common_overrides,
        f"++workload.output.folder={workload_output}",
        f"hydra.run.dir={workload_output}",
        *phase_overrides,
    ]


def run_valgrind(
    test_name: str,
    cmd: list[str],
    log_dir: Path,
    suppression_files: Iterable[Path],
    env: dict[str, str],
    timeout: int,
    track_origins: str,
    num_callers: int,
) -> tuple[int, Path, Path]:
    valgrind_log = log_dir / f"{test_name}.valgrind.log"
    command_log = log_dir / f"{test_name}.command.log"

    vg_cmd = [
        "valgrind",
        "--tool=memcheck",
        "--leak-check=full",
        "--show-leak-kinds=definite,possible",
        f"--track-origins={track_origins}",
        f"--num-callers={num_callers}",
        "--error-exitcode=99",
        f"--log-file={valgrind_log}",
    ]
    for suppression in suppression_files:
        vg_cmd.append(f"--suppressions={suppression}")
    vg_cmd.extend(cmd)

    printable = " ".join(shlex.quote(part) for part in vg_cmd)
    log(f"[valgrind-dlio] RUN {test_name}: {printable}")
    with command_log.open("w", encoding="utf-8", errors="replace") as out:
        out.write(f"$ {printable}\n\n")
        try:
            proc = subprocess.run(
                vg_cmd,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                timeout=timeout,
            )
            out.write(as_text(proc.stdout))
            rc = proc.returncode
        except subprocess.TimeoutExpired as exc:
            out.write(as_text(exc.stdout))
            out.write(f"\n[valgrind-dlio] timeout after {timeout} seconds\n")
            rc = 124
    return rc, valgrind_log, command_log


def run_gdb(
    test_name: str,
    cmd: list[str],
    log_dir: Path,
    env: dict[str, str],
    timeout: int,
) -> tuple[int, Path]:
    gdb_log = log_dir / f"{test_name}.gdb.log"
    gdb_cmd = [
        "gdb",
        "--batch",
        "-q",
        "-ex",
        "set pagination off",
        "-ex",
        "set print thread-events off",
        "-ex",
        "run",
        "-ex",
        "bt full",
        "-ex",
        "thread apply all bt full",
        "-ex",
        "info sharedlibrary",
        "--args",
        *cmd,
    ]
    printable = " ".join(shlex.quote(part) for part in gdb_cmd)
    log(f"[valgrind-dlio] GDB {test_name}: {printable}")
    with gdb_log.open("w", encoding="utf-8", errors="replace") as out:
        out.write(f"$ {printable}\n\n")
        try:
            proc = subprocess.run(
                gdb_cmd,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                timeout=timeout,
            )
            out.write(as_text(proc.stdout))
            rc = proc.returncode
        except subprocess.TimeoutExpired as exc:
            out.write(as_text(exc.stdout))
            out.write(f"\n[valgrind-dlio] gdb timeout after {timeout} seconds\n")
            rc = 124
    return rc, gdb_log


def write_summary(summary_path: Path, payload: dict) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)


def print_summary(payload: dict) -> None:
    log("[valgrind-dlio] ===== Summary =====")
    log(f"[valgrind-dlio] selected_tests={payload['selected_tests']}")
    log(f"[valgrind-dlio] valgrind_executed={payload['valgrind_executed']}")
    log(f"[valgrind-dlio] skipped={payload.get('skipped', 0)}")
    log(f"[valgrind-dlio] failures={payload['failures']}")
    if payload.get("skipped_tests"):
        log("[valgrind-dlio] skipped_tests:")
        for item in payload["skipped_tests"]:
            log(f"  - {item['name']}: {item['reason']}")
    if payload["failed_tests"]:
        log("[valgrind-dlio] failing_tests:")
        for item in payload["failed_tests"]:
            log(f"  - {item['name']}: rc={item['returncode']} {item['error_summary']}")
            log(f"    {item['leak_line']}")
            if item.get("gdb_log"):
                log(f"    gdb_log: {item['gdb_log']}")


def main() -> int:
    args = parse_args()
    focus_dftracer_only = args.focus_dftracer_leaks_only or args.fail_on_project_leaks_only

    # Keep progress output visible in CI/non-interactive environments.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(line_buffering=True)

    if shutil.which("valgrind") is None:
        raise SystemExit("valgrind not found in PATH")
    if shutil.which("gdb") is None:
        raise SystemExit("gdb not found in PATH")

    configs_dir, dlio_package_dir = resolve_configs_dir(args.configs_dir)
    workloads = discover_workloads(configs_dir, args.workload, args.exclude_workload)
    if not workloads:
        raise SystemExit(f"No DLIO workload configs selected from {configs_dir}")

    suppression_files = [Path(path).resolve() for path in args.suppression]
    for suppression in suppression_files:
        if not suppression.is_file():
            raise SystemExit(f"Valgrind suppression file not found: {suppression}")

    log_dir = Path(args.log_dir).resolve()
    run_root = Path(args.run_root).resolve()
    summary_json = Path(args.summary_json).resolve()
    data_root = run_root / "data"
    output_root = run_root / "output"

    if args.clean_run_root:
        shutil.rmtree(run_root, ignore_errors=True)
    log_dir.mkdir(parents=True, exist_ok=True)
    data_root.mkdir(parents=True, exist_ok=True)
    output_root.mkdir(parents=True, exist_ok=True)

    phases = selected_phases(args.phase)
    if args.dftracer_enable == "1" and "train" in phases:
        verify_dftracer_available()

    env = os.environ.copy()
    env.update(
        {
            "DFTRACER_ENABLE": args.dftracer_enable,
            "DFTRACER_INC_METADATA": "1",
            "DFTRACER_LOG_LEVEL": args.log_level,
            "DFTRACER_BIND_SIGNALS": "0",
            "DLIO_LOG_LEVEL": "info",
            "RDMAV_FORK_SAFE": "1",
        }
    )
    gdb_env = env.copy()
    gdb_env["DFTRACER_LOG_LEVEL"] = args.gdb_log_level
    gdb_env["DFTRACER_BIND_SIGNALS"] = "1"

    log(f"[valgrind-dlio] dlio_benchmark package: {dlio_package_dir}")
    log(f"[valgrind-dlio] workload configs: {configs_dir}")
    log(f"[valgrind-dlio] selected phases: {phases}")
    log(f"[valgrind-dlio] excluded workloads: {sorted(args.exclude_workload)}")
    log(f"[valgrind-dlio] selected workloads ({len(workloads)}): {workloads}")
    if focus_dftracer_only:
        log(f"[valgrind-dlio] failing only on project leak frames matching: {args.project_frame_regex}")
    ignore_context_regexes = list(args.ignore_external_context_regex)
    if not args.no_default_external_ignores:
        ignore_context_regexes = [*DEFAULT_EXTERNAL_CONTEXT_IGNORES, *ignore_context_regexes]
    ignore_context_patterns = [re.compile(pattern) for pattern in ignore_context_regexes]
    if ignore_context_regexes:
        log("[valgrind-dlio] ignoring known external contexts:")
        for pattern in ignore_context_regexes:
            log(f"[valgrind-dlio]   {pattern}")

    failed: list[dict] = []
    skipped: list[dict] = []
    total_tests = len(workloads) * len(phases)
    test_index = 0
    for workload in workloads:
        for phase in phases:
            name = f"{workload}.{phase}"
            skip_reason = phase_skip_reason(configs_dir, workload, phase)
            if skip_reason is not None:
                log(f"[valgrind-dlio] SKIP {name}: {skip_reason}")
                skipped.append({"name": name, "reason": skip_reason})
                continue
            cmd = [sys.executable, "-m", "dlio_benchmark.main", *build_overrides(workload, phase, data_root, output_root)]
            test_index += 1
            log(f"[valgrind-dlio] START {test_index}/{total_tests}: {name}")
            test_name = safe_name(name)
            rc, valgrind_log, command_log = run_valgrind(
                test_name=test_name,
                cmd=cmd,
                log_dir=log_dir,
                suppression_files=suppression_files,
                env=env,
                timeout=args.timeout,
                track_origins=args.valgrind_track_origins,
                num_callers=args.valgrind_num_callers,
            )
            print_command_log_excerpt(command_log, name, args.inline_command_output_lines)
            error_summary, leak_line = summarize_log(valgrind_log)
            error_excerpt = extract_error_excerpt(valgrind_log)
            has_vg_errors = "ERROR SUMMARY: 0 errors from 0 contexts" not in error_summary
            actionable, ignored_context_count = actionable_contexts(
                valgrind_log, ignore_context_patterns, args.project_frame_regex
            )
            actionable_context_count, actionable_excerpt = extract_actionable_excerpt(
                actionable, ignore_context_patterns
            )
            project_leak_context_count, project_leak_excerpt = extract_project_leak_excerpt(
                valgrind_log, args.project_frame_regex, ignore_context_patterns
            )

            log(f"[valgrind-dlio] {name} rc={rc}")
            if focus_dftracer_only:
                if project_leak_context_count:
                    log(f"[valgrind-dlio] {name} dftracer leak contexts={project_leak_context_count}")
                elif has_vg_errors:
                    log(
                        f"[valgrind-dlio] {name} dftracer leak contexts=0 "
                        f"(ignored external Valgrind noise; log: {valgrind_log})"
                    )
                if ignored_context_count and actionable_context_count == 0:
                    log(
                        f"[valgrind-dlio] {name} ignored "
                        f"{ignored_context_count} known external Valgrind contexts"
                    )
                if args.show_external_leak_summary:
                    log(f"[valgrind-dlio] {name} {error_summary}")
                    log(f"[valgrind-dlio] {name} {leak_line}")
            else:
                log(f"[valgrind-dlio] {name} {error_summary}")
                log(f"[valgrind-dlio] {name} {leak_line}")

            if focus_dftracer_only:
                failed_test = rc not in (0, 99) or project_leak_context_count > 0
            else:
                failed_test = (rc != 0 and rc != 99) or (has_vg_errors and actionable_context_count > 0)

            if failed_test:
                timed_out = rc == 124
                signal_crash = rc < 0
                if signal_crash:
                    import signal as _signal
                    try:
                        sig_name = _signal.Signals(-rc).name
                    except ValueError:
                        sig_name = f"signal {-rc}"
                    failure_reason = f"signal_crash_{sig_name}"
                    log(f"[valgrind-dlio] {name} SIGNAL CRASH: {sig_name} (rc={rc})")
                elif timed_out:
                    failure_reason = "timeout"
                else:
                    failure_reason = "valgrind_or_command_failure"
                log(
                    f"[valgrind-dlio] {name} actionable_contexts={actionable_context_count} "
                    f"ignored_external_contexts={ignored_context_count}"
                )
                log(f"[valgrind-dlio] {name} valgrind_log={valgrind_log}")
                if signal_crash:
                    pass  # signal crash already logged above with signal name
                elif timed_out:
                    log(
                        f"[valgrind-dlio] {name} timed out after {args.timeout} seconds "
                        "(this is not a leak signal by itself)"
                    )
                if actionable_excerpt and actionable_excerpt != "(no actionable contexts)":
                    log("[valgrind-dlio] --- inline actionable context ---")
                    for line in actionable_excerpt.splitlines()[: args.inline_error_context_lines]:
                        log(f"[valgrind-dlio] {line}")
                elif (
                    not focus_dftracer_only
                    and error_excerpt
                    and error_excerpt != "(no parsed error excerpt)"
                ):
                    log("[valgrind-dlio] --- inline parsed excerpt ---")
                    for line in error_excerpt.splitlines()[: args.inline_error_context_lines]:
                        log(f"[valgrind-dlio] {line}")
                elif focus_dftracer_only and actionable_context_count == 0 and has_vg_errors:
                    log(
                        "[valgrind-dlio] inline excerpt suppressed because no actionable "
                        "dftracer contexts were detected"
                    )

                gdb_rc = None
                gdb_log = None
                if signal_crash or (not timed_out) or args.gdb_on_timeout:
                    gdb_rc, gdb_log = run_gdb(
                        test_name=test_name,
                        cmd=cmd,
                        log_dir=log_dir,
                        env=gdb_env,
                        timeout=args.timeout,
                    )
                    print_gdb_log_excerpt(gdb_log, name, args.inline_gdb_output_lines)
                else:
                    log(f"[valgrind-dlio] {name} skipping gdb rerun for timeout failure")

                failure_item = {
                    "name": name,
                    "returncode": rc,
                    "failure_reason": failure_reason,
                    "valgrind_log": str(valgrind_log),
                    "command_log": str(command_log),
                    "error_summary": error_summary,
                    "leak_line": leak_line,
                    "error_excerpt": error_excerpt,
                    "project_leak_context_count": project_leak_context_count,
                    "project_leak_excerpt": project_leak_excerpt,
                    "ignored_external_context_count": ignored_context_count,
                    "actionable_context_count": actionable_context_count,
                    "actionable_error_excerpt": actionable_excerpt,
                }
                if gdb_log is not None:
                    failure_item["gdb_returncode"] = gdb_rc
                    failure_item["gdb_log"] = str(gdb_log)
                failed.append(failure_item)

                if phase == "generate":
                    log(f"[valgrind-dlio] skipping {workload}.train because generate failed")
                    break

    payload = {
        "selected_tests": total_tests,
        "selected_workloads": workloads,
        "selected_phases": phases,
        "excluded_workloads": sorted(args.exclude_workload),
        "valgrind_executed": test_index,
        "skipped": len(skipped),
        "skipped_tests": skipped,
        "failures": len(failed),
        "failed_tests": failed,
    }
    write_summary(summary_json, payload)
    print_summary(payload)

    if failed:
        log("[valgrind-dlio] FAIL: DLIO valgrind errors or command failures detected")
        for item in failed:
            log(f"  - {item['name']}: {item['valgrind_log']}")
            if item.get("failure_reason") == "timeout":
                log("    failure_reason: timeout")
            log(f"    command_log: {item['command_log']}")
            log(f"    {item['error_summary']}")
            log(f"    {item['leak_line']}")
            if "gdb_log" in item:
                log(f"    gdb_log: {item['gdb_log']}")
            project_excerpt = item.get("project_leak_excerpt")
            if project_excerpt and project_excerpt != "(no project-leak contexts)":
                log("    --- dftracer leak excerpt ---")
                for line in project_excerpt.splitlines()[:40]:
                    log(f"    {line}")
            actionable_excerpt = item.get("actionable_error_excerpt")
            if actionable_excerpt and actionable_excerpt != "(no actionable contexts)":
                log("    --- actionable excerpt ---")
                for line in actionable_excerpt.splitlines()[:40]:
                    log(f"    {line}")
            excerpt = item.get("error_excerpt")
            if (
                not focus_dftracer_only
                and excerpt
                and excerpt != "(no parsed error excerpt)"
            ):
                log("    --- excerpt ---")
                for line in excerpt.splitlines()[:40]:
                    log(f"    {line}")
            elif (
                focus_dftracer_only
                and item.get("actionable_context_count", 0) == 0
                and item.get("project_leak_context_count", 0) == 0
                and item.get("error_excerpt")
                and item.get("error_excerpt") != "(no parsed error excerpt)"
            ):
                log("    generic valgrind excerpt suppressed (no actionable dftracer contexts)")
        return 1

    log("[valgrind-dlio] PASS: all DLIO phases clean under valgrind")
    return 0


if __name__ == "__main__":
    sys.exit(main())
