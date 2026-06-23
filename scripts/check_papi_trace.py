#!/usr/bin/env python3

from __future__ import annotations

import argparse
import gzip
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate DFTracer PAPI traces by counting events and checking for configured PAPI counters"
    )
    parser.add_argument(
        "trace_files",
        nargs="+",
        help="Trace files to inspect (.pfw, .gz, .pfw.gz)",
    )
    parser.add_argument(
        "--min-events",
        type=int,
        required=True,
        help="Minimum total JSON events expected across all traces",
    )
    parser.add_argument(
        "--require-papi-event",
        action="append",
        default=[],
        help="PAPI event name that must appear at least once in the trace payload; can be repeated",
    )
    parser.add_argument(
        "--min-papi-lines",
        type=int,
        default=1,
        help="Minimum number of JSON events mentioning any required PAPI event",
    )
    return parser.parse_args()


def iter_trace_lines(path: Path):
    opener = gzip.open if path.suffix == ".gz" or path.name.endswith(".pfw.gz") else open
    with opener(path, "rt", encoding="utf-8", errors="replace") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line or line in {"[", "]"}:
                continue
            if line.endswith(","):
                line = line[:-1].rstrip()
            if line and line[0] in "[{":
                yield line


def main() -> int:
    args = parse_args()
    required_events = [event for event in args.require_papi_event if event]

    total_events = 0
    papi_hits = {event: 0 for event in required_events}
    json_errors: list[str] = []

    for trace_name in args.trace_files:
        trace_path = Path(trace_name)
        if not trace_path.is_file():
            print(f"missing trace file: {trace_path}", file=sys.stderr)
            return 1

        for line_number, line in enumerate(iter_trace_lines(trace_path), start=1):
            try:
                payload = json.loads(line)
            except json.JSONDecodeError as exc:
                json_errors.append(f"{trace_path}:{line_number}: {exc}")
                continue

            total_events += 1

            if required_events:
                serialized = json.dumps(payload, sort_keys=True)
                for event in required_events:
                    if event in serialized:
                        papi_hits[event] += 1

    if json_errors:
        print("failed to parse trace JSON:", file=sys.stderr)
        for entry in json_errors[:10]:
            print(entry, file=sys.stderr)
        return 1

    if total_events < args.min_events:
        print(
            f"expected at least {args.min_events} events, found {total_events}",
            file=sys.stderr,
        )
        return 1

    total_papi_lines = sum(papi_hits.values()) if papi_hits else 0
    if required_events and total_papi_lines < args.min_papi_lines:
        print(
            f"expected at least {args.min_papi_lines} PAPI-tagged events, found {total_papi_lines}",
            file=sys.stderr,
        )
        return 1

    missing = [event for event, count in papi_hits.items() if count == 0]
    if missing:
        print(
            "missing required PAPI events: " + ", ".join(missing),
            file=sys.stderr,
        )
        return 1

    print(f"validated {total_events} total events across {len(args.trace_files)} trace file(s)")
    if required_events:
        summary = ", ".join(f"{event}={papi_hits[event]}" for event in required_events)
        print(f"validated PAPI events: {summary}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())