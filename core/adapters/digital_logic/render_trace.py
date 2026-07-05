#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2] / "artifacts/full_adder_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a full-adder signal trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        rows = [item for item in records if item.get("type") == "truth_row"]
        safety = next(
            item for item in records if item.get("type") == "safety_case"
        )
        if len(rows) != 8:
            raise ValueError("trace must contain eight truth rows")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Finite full-adder signal validation\n")
    print("A B Cin | Sum Carry | Signal subunits")
    for row in rows:
        print(
            f"{row['A']} {row['B']}  {row['carry_in']}  |"
            f"  {row['sum_bit']}    {row['carry_bit']}   |"
            f" {row['sum_payload']} + {row['carry_payload']}"
        )
    print(
        "\nInvalid output fabrication rejected: "
        f"{safety['invalid_gate_rejected']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
