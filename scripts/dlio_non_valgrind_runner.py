#!/usr/bin/env python3
"""Run DLIO workload phases without valgrind and rerun failures with gdb."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import shlex
import shutil
import subprocess
import sys
import traceback
from pathlib import Path

import yaml


def log(message: str) -> None:
    print(message, flush=True)


def as_text(value: str | bytes | None) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run DLIO workloads without valgrind")
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
        help="Directory for command and gdb logs.",
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
        "--timeout",
        type=int,
        default=600,
        help="Timeout in seconds for each run/gdb phase.",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        help="DFTRACER_LOG_LEVEL for non-valgrind runs.",
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


def print_command_log_excerpt(command_log: Path, label: str, max_lines: int = 80) -> None:
    if not command_log.exists():
        log(f"[dlio-run] {label} command output missing: {command_log}")
        return

    lines = command_log.read_text(encoding="utf-8", errors="replace").splitlines()
    body = lines
    if lines and lines[0].startswith("$ "):
        body = lines[2:] if len(lines) >= 2 and lines[1] == "" else lines[1:]

    if not body:
        log(f"[dlio-run] {label} command output: (empty)")
        return

    snippet = body[-max_lines:]
    log(f"[dlio-run] --- command output ({label}, last {len(snippet)} lines) ---")
    for line in snippet:
        log(f"[dlio-run] {line}")


def print_gdb_log_excerpt(gdb_log: Path, label: str, max_lines: int = 80) -> None:
    if not gdb_log.exists():
        log(f"[dlio-run] {label} gdb output missing: {gdb_log}")
        return

    lines = gdb_log.read_text(encoding="utf-8", errors="replace").splitlines()
    body = lines
    if lines and lines[0].startswith("$ "):
        body = lines[2:] if len(lines) >= 2 and lines[1] == "" else lines[1:]

    if not body:
        log(f"[dlio-run] {label} gdb output: (empty)")
        return

    snippet = body[-max_lines:]
    log(f"[dlio-run] --- gdb output ({label}, last {len(snippet)} lines) ---")
    for line in snippet:
        log(f"[dlio-run] {line}")


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
    return "".join(ch if ch.isalnum() or ch in "_.-" else "_" for ch in value)


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
        log(f"[non-valgrind-dlio] DFTracer native module loaded: {module_path}")
    except Exception:  # pragma: no cover
        log("[non-valgrind-dlio] ERROR: DFTRACER_ENABLE=1 but dftracer native module failed to import")
        traceback.print_exc()
        raise SystemExit(2)


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


def run_command(
    test_name: str,
    cmd: list[str],
    log_dir: Path,
    env: dict[str, str],
    timeout: int,
) -> tuple[int, Path]:
    command_log = log_dir / f"{test_name}.command.log"
    printable = " ".join(shlex.quote(part) for part in cmd)
    log(f"[dlio-run] RUN {test_name}: {printable}")
    with command_log.open("w", encoding="utf-8", errors="replace") as out:
        out.write(f"$ {printable}\n\n")
        try:
            proc = subprocess.run(
                cmd,
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
            out.write(f"\n[dlio-run] timeout after {timeout} seconds\n")
            rc = 124
    return rc, command_log


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
    log(f"[dlio-run] GDB {test_name}: {printable}")
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
            out.write(f"\n[dlio-run] gdb timeout after {timeout} seconds\n")
            rc = 124
    return rc, gdb_log


def write_summary(summary_path: Path, payload: dict) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    with summary_path.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)


def print_summary(payload: dict) -> None:
    log("[dlio-run] ===== Summary =====")
    log(f"[dlio-run] selected_tests={payload['selected_tests']}")
    log(f"[dlio-run] executed={payload['executed']}")
    log(f"[dlio-run] skipped={payload.get('skipped', 0)}")
    log(f"[dlio-run] failures={payload['failures']}")
    if payload.get("skipped_tests"):
        log("[dlio-run] skipped_tests:")
        for item in payload["skipped_tests"]:
            log(f"  - {item['name']}: {item['reason']}")
    if payload["failed_tests"]:
        log("[dlio-run] failing_tests:")
        for item in payload["failed_tests"]:
            log(f"  - {item['name']}: rc={item['returncode']}")
            log(f"    command_log: {item['command_log']}")
            if item.get("gdb_log"):
                log(f"    gdb_log: {item['gdb_log']}")


def main() -> int:
    args = parse_args()

    if shutil.which("gdb") is None:
        raise SystemExit("gdb not found in PATH")

    configs_dir, dlio_package_dir = resolve_configs_dir(args.configs_dir)
    workloads = discover_workloads(configs_dir, args.workload, args.exclude_workload)
    if not workloads:
        raise SystemExit(f"No DLIO workload configs selected from {configs_dir}")

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

    log(f"[dlio-run] dlio_benchmark package: {dlio_package_dir}")
    log(f"[dlio-run] workload configs: {configs_dir}")
    log(f"[dlio-run] selected phases: {phases}")
    log(f"[dlio-run] excluded workloads: {sorted(args.exclude_workload)}")
    log(f"[dlio-run] selected workloads ({len(workloads)}): {workloads}")

    failed: list[dict] = []
    skipped: list[dict] = []
    total_tests = len(workloads) * len(phases)
    test_index = 0
    for workload in workloads:
        for phase in phases:
            test_name = f"{workload}.{phase}"
            skip_reason = phase_skip_reason(configs_dir, workload, phase)
            if skip_reason is not None:
                log(f"[dlio-run] SKIP {test_name}: {skip_reason}")
                skipped.append({"name": test_name, "reason": skip_reason})
                continue
            cmd = [
                sys.executable,
                "-m",
                "dlio_benchmark.main",
                *build_overrides(workload, phase, data_root, output_root),
            ]
            test_index += 1
            log(f"[dlio-run] START {test_index}/{total_tests}: {test_name}")
            rc, command_log = run_command(
                test_name=safe_name(test_name),
                cmd=cmd,
                log_dir=log_dir,
                env=env,
                timeout=args.timeout,
            )
            print_command_log_excerpt(command_log, test_name, args.inline_command_output_lines)
            log(f"[dlio-run] {test_name} rc={rc}")
            if rc != 0:
                gdb_rc = None
                gdb_log = None
                if phase in {"train", "checkpoint"}:
                    gdb_rc, gdb_log = run_gdb(
                        test_name=safe_name(test_name),
                        cmd=cmd,
                        log_dir=log_dir,
                        env=gdb_env,
                        timeout=args.timeout,
                    )
                    print_gdb_log_excerpt(gdb_log, test_name, args.inline_gdb_output_lines)

                failure_item = {
                    "name": test_name,
                    "returncode": rc,
                    "command_log": str(command_log),
                }
                if gdb_log is not None:
                    failure_item["gdb_returncode"] = gdb_rc
                    failure_item["gdb_log"] = str(gdb_log)
                failed.append(failure_item)

                if phase == "generate" and "train" in phases:
                    log(f"[dlio-run] skipping {workload}.train because generate failed")
                    break

    payload = {
        "selected_tests": total_tests,
        "selected_workloads": workloads,
        "selected_phases": phases,
        "excluded_workloads": sorted(args.exclude_workload),
        "executed": test_index,
        "skipped": len(skipped),
        "skipped_tests": skipped,
        "failures": len(failed),
        "failed_tests": failed,
    }
    write_summary(summary_json, payload)
    print_summary(payload)

    if failed:
        log("[dlio-run] FAIL: DLIO non-valgrind command failures detected")
        for item in failed:
            log(f"  - {item['name']}: rc={item['returncode']}")
            log(f"    command_log: {item['command_log']}")
            if "gdb_log" in item:
                log(f"    gdb_log: {item['gdb_log']}")
        return 1

    log("[dlio-run] PASS: all DLIO phases succeeded without valgrind")
    return 0


if __name__ == "__main__":
    sys.exit(main())
