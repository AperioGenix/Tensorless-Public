#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/protein_lattice_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render an HP lattice candidate trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        candidates = [
            item for item in records if item.get("type") == "candidate"
        ]
        packing = next(
            item for item in records
            if item.get("type") == "steric_contention"
        )
        if len(candidates) != 3:
            raise ValueError("trace must contain three candidates")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("HP protein-lattice candidate validation\n")
    for candidate in candidates:
        print(
            f"{candidate['name']:10} "
            f"connected={candidate['chain_connected']} "
            f"self_avoiding={candidate['self_avoiding']} "
            f"contacts={candidate['hydrophobic_contacts']} "
            f"energy={candidate['energy']}"
        )
    print("\nSteric packing contention")
    print(f"  demand   {packing['demands']}")
    print(f"  accepted {packing['accepted']}")
    print(f"  exhaust  {packing['exhaust']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
