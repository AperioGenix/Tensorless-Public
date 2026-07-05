#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2] / "artifacts/limit_order_trace.jsonl"
)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render a finite limit-order execution trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        levels = [item for item in records if item.get("type") == "sweep_level"]
        fok = next(item for item in records if item.get("type") == "fok_order")
        failed = next(
            item for item in records if item.get("type") == "failed_matcher"
        )
        summary = next(
            item for item in records if item.get("type") == "summary"
        )
        if len(levels) != 2:
            raise ValueError("trace must contain two sweep levels")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Finite limit-order execution\n")
    for level in levels:
        print(
            f"Level {level['price_cents']} cents: "
            f"filled {level['filled_shares']} shares, "
            f"cash {level['cash_cents']} cents"
        )
    print(
        f"Sweep total: {summary['aggregate_filled_shares']} shares, "
        f"VWAP {summary['integer_vwap_cents']} cents"
    )
    print(
        f"FOK cancelled: {fok['cancelled']} "
        f"(eligible {fok['eligible_shares']} of "
        f"{fok['requested_shares']})"
    )
    print(
        "Failed matcher cancelled request shares: "
        f"{failed['external_exhaust']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
