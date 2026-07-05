#!/usr/bin/env python3

import argparse
import json
import sys
import time
from pathlib import Path


DEFAULT_TRACE = Path(__file__).parents[2] / "artifacts/diffusion_trace.jsonl"
SHADES = " .:-=+*#%@"


def load_frames(path: Path) -> list[dict]:
    with path.open(encoding="utf-8") as stream:
        frames = [
            record
            for line in stream
            if (record := json.loads(line)).get("type") == "frame"
        ]
    if not frames:
        raise ValueError("trace contains no diffusion frames")
    return frames


def main() -> int:
    parser = argparse.ArgumentParser(description="Render a diffusion JSONL trace.")
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    parser.add_argument("--delay", type=float, default=0.4)
    parser.add_argument("--no-clear", action="store_true")
    args = parser.parse_args()
    if args.delay < 0:
        parser.error("--delay cannot be negative")

    try:
        frames = load_frames(args.trace)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    for frame in frames:
        if not args.no_clear:
            sys.stdout.write("\033[2J\033[H")
        masses = {(x, y): mass for x, y, mass in frame["z_slice"]}
        maximum = max(frame["maximum_mass"], 1)
        for y in range(19):
            row = []
            for x in range(19):
                mass = masses.get((x, y), 0)
                shade = SHADES[min(len(SHADES) - 1, mass * len(SHADES) // maximum)]
                row.append(shade)
            print(" ".join(row))
        print(
            f"\nTick: {frame['tick']} | Active 3D nodes: "
            f"{frame['active_nodes']} | Slice max: {frame['maximum_mass']}"
        )
        sys.stdout.flush()
        time.sleep(args.delay)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
