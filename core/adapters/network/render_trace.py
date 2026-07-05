#!/usr/bin/env python3

import argparse
import json
import sys
import time
from pathlib import Path


DEFAULT_TRACE = Path(__file__).parents[2] / "artifacts/switch_trace.jsonl"


def bar(value: int) -> str:
    return "█" * value if value else "·"


def main() -> int:
    parser = argparse.ArgumentParser(description="Render a switch-fabric trace.")
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    parser.add_argument("--delay", type=float, default=0.3)
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
        print("trace contains no switch frames", file=sys.stderr)
        return 2

    for frame in frames:
        if not args.no_clear:
            sys.stdout.write("\033[2J\033[H")
        print(f"Tick {frame['tick']:2}\n")
        print(f"X offered  {bar(frame['offered'][0])} ({frame['offered'][0]})")
        print(f"Y offered  {bar(frame['offered'][1])} ({frame['offered'][1]})")
        print(f"X accepted {bar(frame['accepted'][0])} ({frame['accepted'][0]})")
        print(f"Y accepted {bar(frame['accepted'][1])} ({frame['accepted'][1]})")
        print(f"X queued   {bar(frame['remaining'][0])} ({frame['remaining'][0]})")
        print(f"Y queued   {bar(frame['remaining'][1])} ({frame['remaining'][1]})")
        sys.stdout.flush()
        time.sleep(args.delay)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
