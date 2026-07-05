#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/stoichiometric_batch_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a stoichiometric batch trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        states = [item for item in records if item.get("type") == "batch_state"]
        if len(states) != 3:
            raise ValueError("trace must contain three batch states")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Finite stoichiometric batch: A + B -> AB\n")
    for state in states:
        print(state["state"].replace("_", " ").title())
        print(
            f"  retained A/B  {state['retained_A']} / "
            f"{state['retained_B']}"
        )
        print(
            f"  accepted A/B  {state['accepted_A']} / "
            f"{state['accepted_B']}"
        )
        print(
            f"  exhaust A/B   {state['exhaust_A']} / "
            f"{state['exhaust_B']}"
        )
        print(f"  AB pairs      {state['product_AB_pairs']}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
