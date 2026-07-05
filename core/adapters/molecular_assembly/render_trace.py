#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/molecular_tile_assembly_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a seeded XOR tile-assembly trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        assemblies = [
            record for record in records if record.get("type") == "assembly"
        ]
        safety = [
            record for record in records if record.get("type") == "safety_case"
        ]
        if len(assemblies) != 3 or len(safety) != 2:
            raise ValueError(
                "trace must contain three assemblies and two safety cases"
            )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Seeded XOR tile assembly\n")
    for assembly in assemblies:
        print(
            f"{assembly['name']:24} "
            f"filled={assembly['sites_filled']:2} "
            f"tokens={assembly['tokens_consumed']:2}/"
            f"{assembly['initial_tokens']:2} "
            f"rows={'/'.join(assembly['rows'])}"
        )
    print()
    for case in safety:
        print(
            f"{case['name']:24} proposals={case['proposals']} "
            f"accepted={case['accepted']} "
            f"released={case['reservations_released']} "
            f"invalid_exhaust={case['invalid_proposal_exhaust']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
