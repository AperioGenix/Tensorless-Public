#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/qpu_hardware_fixture_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a canonical QPU-result audit trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        result = next(
            record for record in records if record.get("type") == "qpu_result"
        )
        routing = next(
            record for record in records if record.get("type") == "routing"
        )
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    scale = result["metric_scale"]
    print("Quantum hardware result audit\n")
    print(
        f"provider={result['provider']} backend={result['backend']} "
        f"job={result['job_id']} synthetic={result['synthetic']}"
    )
    print(
        f"observed={result['counts']} ideal_counts={result['ideal_counts']} "
        f"residuals={result['signed_count_residuals']}"
    )
    print(
        f"TVD={result['total_variation_q32'] / scale:.8f} "
        f"Hellinger={result['hellinger_q32'] / scale:.8f}"
    )
    print(
        f"routed={routing['accepted']} "
        f"capacity_exhaust={routing['detector_capacity_exhaust']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
