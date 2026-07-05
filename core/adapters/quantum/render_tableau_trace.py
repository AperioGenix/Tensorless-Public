#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/stabilizer_tableau_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a multi-qubit stabilizer-tableau trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        samples = {
            item["name"]: item
            for item in records
            if item.get("type") == "sample"
        }
        scale = next(
            item for item in records if item.get("type") == "scale_case"
        )
        routing = next(
            item for item in records if item.get("type") == "routing"
        )
        safety = next(
            item for item in records if item.get("type") == "safety_case"
        )
        for name in (
            "bell",
            "inverse_bell",
            "phase_bell",
            "product_x_q0",
        ):
            if name not in samples:
                raise ValueError(f"trace is missing sample {name}")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Multi-qubit stabilizer tableau\n")
    for name in ("bell", "inverse_bell", "phase_bell", "product_x_q0"):
        sample = samples[name]
        print(f"{name:12} counts={sample['counts']}")
    print(
        f"\n{scale['name']}: {scale['qubits']} qubits, "
        f"{scale['shots']} shots, "
        f"outcomes={scale['observed_outcomes']} "
        f"(zero={scale['all_zero']}, one={scale['all_one']})"
    )
    print(
        f"Routing accepted={routing['accepted']}, "
        f"exhaust={routing['external_exhaust']}"
    )
    print(
        f"Saturation accepted={safety['accepted']}, "
        f"exhaust={safety['external_exhaust']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
