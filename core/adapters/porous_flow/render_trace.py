#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2] / "artifacts/porous_flow_trace.jsonl"
)


def bar(value: int, maximum: int = 10000, width: int = 40) -> str:
    filled = value * width // maximum
    return "█" * filled + "░" * (width - filled)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a porous-flow capacity trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        states = [item for item in records if item.get("type") == "flow_state"]
        if len(states) != 3:
            raise ValueError("trace must contain three flow states")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Finite porous-flow capacity\n")
    for state in states:
        print(state["state"].replace("_", " ").title())
        for label, key in (
            ("Sandstone", "sandstone_delivered"),
            ("Fracture", "fracture_delivered"),
            ("Retained", "retained_manifold_pressure"),
            ("Exhaust", "external_exhaust"),
        ):
            value = state[key]
            print(f"  {label:9} {bar(value)} {value}")
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
