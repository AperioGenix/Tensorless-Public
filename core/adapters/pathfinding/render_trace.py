#!/usr/bin/env python3

import argparse
import json
import sys
import time
from pathlib import Path


GRID_WIDTH = 15
GRID_HEIGHT = 9
SOURCE = (1, 4)
GOAL = (13, 4)
DYNAMIC_WALL = (6, 4)

GREEN = "\033[92m"
RED = "\033[91m"
RESET = "\033[0m"
CLEAR = "\033[2J\033[H"


def load_hops(path: Path) -> list[dict]:
    hops: list[dict] = []
    with path.open("r", encoding="utf-8") as trace_file:
        for line_number, line in enumerate(trace_file, start=1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid JSON: {error.msg}"
                ) from error
            if record.get("type") != "hop":
                continue
            required = {"tick", "epoch", "from", "to"}
            missing = required.difference(record)
            if missing:
                names = ", ".join(sorted(missing))
                raise ValueError(
                    f"{path}:{line_number}: missing hop fields: {names}"
                )
            hops.append(record)

    if not hops:
        raise ValueError(f"{path}: trace contains no hop records")
    return hops


def render_frame(
    position: tuple[int, int],
    tick: int,
    epoch: int,
    use_color: bool,
    clear_screen: bool,
) -> None:
    if clear_screen:
        sys.stdout.write(CLEAR)

    wall_active = epoch >= 1
    for y in range(GRID_HEIGHT):
        cells: list[str] = []
        for x in range(GRID_WIDTH):
            point = (x, y)
            if point == position:
                token = "O"
                if use_color:
                    token = f"{GREEN}{token}{RESET}"
            elif point == DYNAMIC_WALL and wall_active:
                token = "X"
                if use_color:
                    token = f"{RED}{token}{RESET}"
            elif point == SOURCE:
                token = "S"
            elif point == GOAL:
                token = "G"
            else:
                token = "."
            cells.append(f" {token} ")
        print("".join(cells))

    print(
        f"\nTick: {tick} | Epoch: {epoch} | "
        f"Position: [{position[0]}, {position[1]}]"
    )
    sys.stdout.flush()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Animate a Tensorless pathfinding JSONL trace."
    )
    parser.add_argument(
        "trace",
        nargs="?",
        type=Path,
        default=Path(__file__).with_name("pathfinding_trace.jsonl"),
        help="JSONL trace path (default: pathfinding_trace.jsonl beside this script)",
    )
    parser.add_argument(
        "--delay",
        type=float,
        default=0.4,
        help="seconds between frames (default: 0.4)",
    )
    parser.add_argument(
        "--no-color",
        action="store_true",
        help="disable ANSI colors",
    )
    parser.add_argument(
        "--no-clear",
        action="store_true",
        help="print frames sequentially instead of clearing the terminal",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.delay < 0:
        print("--delay cannot be negative", file=sys.stderr)
        return 2

    try:
        hops = load_hops(args.trace)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 2

    for hop in hops:
        position = tuple(hop["to"])
        if len(position) != 2:
            print("hop destination must contain two coordinates", file=sys.stderr)
            return 2
        render_frame(
            (int(position[0]), int(position[1])),
            int(hop["tick"]),
            int(hop["epoch"]),
            not args.no_color,
            not args.no_clear,
        )
        time.sleep(args.delay)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
