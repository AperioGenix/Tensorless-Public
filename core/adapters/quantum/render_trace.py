#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/stabilizer_circuit_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a stabilizer-circuit trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        circuits = [item for item in records if item.get("type") == "circuit"]
        safety = next(
            item for item in records if item.get("type") == "safety_case"
        )
        if len(circuits) != 3:
            raise ValueError("trace must contain three circuits")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Finite single-qubit stabilizer circuits\n")
    for circuit in circuits:
        print(
            f"{circuit['gates']:5} "
            f"{circuit['initial_state']} -> {circuit['final_state']} "
            f"detectors=[{circuit['detector_0']},"
            f"{circuit['detector_1']}]"
        )
    print(
        "\nDetector saturation: "
        f"accepted [{safety['detector_0']},{safety['detector_1']}], "
        f"exhaust {safety['external_exhaust']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
