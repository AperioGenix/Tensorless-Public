#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/active_torque_vectoring_trace.jsonl"
)


def bar(value: int, maximum: int, width: int = 40) -> str:
    filled = value * width // maximum
    return "█" * filled + "░" * (width - filled)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render an active torque-vectoring trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        controller = next(
            item for item in records if item.get("type") == "controller"
        )
        safety = next(
            item for item in records if item.get("type") == "safety_case"
        )
        wheels = [item for item in records if item.get("type") == "wheel"]
        if len(wheels) != 2:
            raise ValueError("trace must contain two wheel records")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    maximum = max(
        controller["total_force_demand"],
        *(item["longitudinal_capacity"] for item in wheels),
    )
    print("Active rear-axle torque vectoring\n")
    print(
        f"Demand {controller['total_force_demand']}  "
        f"Yaw target {controller['yaw_moment_target']}  "
        f"Achieved {controller['achieved_yaw_moment']}"
    )
    print(
        f"Admitted {controller['admitted_total']}  "
        f"Held upstream {controller['upstream_holdback']}\n"
    )
    for wheel in wheels:
        label = wheel["wheel"].replace("_", " ").title()
        capacity = wheel["longitudinal_capacity"]
        commanded = wheel["commanded_longitudinal_force"]
        delivered = wheel["delivered_longitudinal_force"]
        print(f"{label}")
        print(f"  capacity  {bar(capacity, maximum)} {capacity}")
        print(f"  command   {bar(commanded, maximum)} {commanded}")
        print(f"  delivered {bar(delivered, maximum)} {delivered}\n")

    print("ECU failure: raw 50/50 request")
    print(
        "  Rear left  "
        f"{safety['rear_left_request']} requested, "
        f"{safety['rear_left_delivered']} delivered"
    )
    print(
        "  Rear right "
        f"{safety['rear_right_request']} requested, "
        f"{safety['rear_right_delivered']} delivered"
    )
    print(f"  External exhaust {safety['external_exhaust']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
