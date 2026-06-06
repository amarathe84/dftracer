#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Run selected CTest tests under valgrind and fail on unsuppressed errors.")
    p.add_argument("--build-dir", required=True, help="Path to CMake/CTest build directory")
    p.add_argument("--suppression", required=True, help="Valgrind suppression file")
    p.add_argument("--tests", nargs="+", required=True, help="CTest test names to run")
    return p.parse_args()


def test_env_items(test_obj: dict) -> list[str]:
    for prop in test_obj.get("properties", []):
        if prop.get("name") != "ENVIRONMENT":
            continue
        value = prop.get("value")
        if isinstance(value, list):
            return [x for x in value if isinstance(x, str)]
        if isinstance(value, str):
            return [x for x in value.split(";") if x]
    return []


def run_one(build_dir: Path, suppression: Path, test_obj: dict) -> int:
    name = test_obj["name"]
    cmd = test_obj.get("command") or []
    if not cmd:
        print(f"[valgrind-gate] {name}: missing command")
        return 2

    env = os.environ.copy()
    for kv in test_env_items(test_obj):
        if "=" in kv:
            k, v = kv.split("=", 1)
            env[k] = v
    env["DFTRACER_BIND_SIGNALS"] = "0"

    log_file = build_dir / f"valgrind_gate_{name}.log"
    vg_cmd = [
        "valgrind",
        "--tool=memcheck",
        "--leak-check=full",
        "--show-leak-kinds=definite,possible",
        "--track-origins=yes",
        "--num-callers=40",
        "--error-exitcode=99",
        f"--suppressions={suppression}",
        f"--log-file={log_file}",
        *cmd,
    ]

    print(f"[valgrind-gate] running {name}")
    rc = subprocess.call(vg_cmd, cwd=build_dir, env=env)

    summary = "(missing log)"
    if log_file.exists():
        with log_file.open("r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        for line in lines:
            if "ERROR SUMMARY:" in line:
                summary = line.strip()
                break
        for line in lines:
            if "definitely lost:" in line:
                print(f"[valgrind-gate] {name} {line.strip()}")
                break

    print(f"[valgrind-gate] {name} rc={rc} {summary}")
    return rc


def main() -> int:
    args = parse_args()
    build_dir = Path(args.build_dir).resolve()
    suppression = Path(args.suppression).resolve()

    if not build_dir.is_dir():
        print(f"[valgrind-gate] build dir not found: {build_dir}")
        return 2
    if not suppression.is_file():
        print(f"[valgrind-gate] suppression file not found: {suppression}")
        return 2

    try:
        info = json.loads(subprocess.check_output(["ctest", "--show-only=json-v1"], cwd=build_dir, text=True))
    except Exception as exc:
        print(f"[valgrind-gate] failed to query ctest json: {exc}")
        return 2

    tests_by_name = {t.get("name"): t for t in info.get("tests", [])}
    missing = [t for t in args.tests if t not in tests_by_name]
    if missing:
        print(f"[valgrind-gate] missing tests in ctest json: {', '.join(missing)}")
        return 2

    failed = False
    for test_name in args.tests:
        rc = run_one(build_dir, suppression, tests_by_name[test_name])
        if rc != 0:
            failed = True

    if failed:
        print("[valgrind-gate] unsuppressed valgrind errors detected")
        return 1

    print("[valgrind-gate] all selected tests clean under configured suppressions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
