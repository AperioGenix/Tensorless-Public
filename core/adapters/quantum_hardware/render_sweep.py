#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a QPU physical identity-depth audit."
    )
    parser.add_argument("audit", type=Path)
    args = parser.parse_args()
    try:
        with args.audit.open(encoding="utf-8") as stream:
            records = [
                json.loads(line)
                for line in stream
                if line.strip()
            ]
        results = sorted(
            (
                record
                for record in records
                if record.get("type") == "qpu_result"
            ),
            key=lambda record: record["identity_cycles"],
        )
        if not results:
            raise ValueError("audit contains no QPU results")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("IBM SX identity-depth sweep\n")
    print("cycles  sx gates  counts          TVD         Hellinger")
    for result in results:
        scale = result["metric_scale"]
        cycles = result["identity_cycles"]
        sx_gates = result["isa_gate_counts"].get("sx", 0)
        print(
            f"{cycles:6d}  {sx_gates:8d}  "
            f"{str(result['counts']):14}  "
            f"{result['total_variation_q32'] / scale:.8f}  "
            f"{result['hellinger_q32'] / scale:.8f}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
