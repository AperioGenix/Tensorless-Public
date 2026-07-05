#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2] / "artifacts/silicon_thermal_trace.jsonl"
)


def bar(value: int, maximum: int, width: int = 40) -> str:
    filled = value * width // maximum
    return "█" * filled + "░" * (width - filled)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a silicon thermal-routing trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        states = [
            item for item in records if item.get("type") == "thermal_state"
        ]
        if len(states) != 3:
            raise ValueError("trace must contain three thermal states")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Silicon thermal routing\n")
    for state in states:
        lateral = sum(state["lateral_delivered"])
        print(state["state"].replace("_", " ").title())
        print(f"  TIM       {bar(state['tim_delivered'], 10000)} "
              f"{state['tim_delivered']}")
        print(f"  Lateral   {bar(lateral, 10000)} {lateral}")
        print(
            f"  Retained  {bar(state['retained_source_energy'], 10000)} "
            f"{state['retained_source_energy']}"
        )
        print(
            f"  Exhaust   {bar(state['external_exhaust'], 10000)} "
            f"{state['external_exhaust']}\n"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
