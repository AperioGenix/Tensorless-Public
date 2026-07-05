#!/usr/bin/env python3

import argparse
import json
import sys
import time
from pathlib import Path


DEFAULT_TRACE = Path(__file__).parents[2] / "artifacts/transport_trace.jsonl"


def main() -> int:
    parser = argparse.ArgumentParser(description="Render periodic transport.")
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    parser.add_argument("--delay", type=float, default=0.4)
    parser.add_argument("--no-clear", action="store_true")
    args = parser.parse_args()
    if args.delay < 0:
        parser.error("--delay cannot be negative")
    try:
        with args.trace.open(encoding="utf-8") as stream:
            frames = [
                item
                for line in stream
                if (item := json.loads(line)).get("type") == "frame"
            ]
    except (OSError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2
    if not frames:
        print("trace contains no transport frames", file=sys.stderr)
        return 2

    for frame in frames:
        if not args.no_clear:
            sys.stdout.write("\033[2J\033[H")
        occupied_x = {(frame["start_x"] + offset) % 7 for offset in range(3)}
        for y in range(7):
            print(" ".join("█" if x in occupied_x and 2 <= y < 5 else "·" for x in range(7)))
        print(
            f"\nTick: {frame['tick']} | x start: {frame['start_x']} | "
            f"3D occupied cells: {frame['occupied_cells']}"
        )
        sys.stdout.flush()
        time.sleep(args.delay)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
