#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path


DEFAULT_TRACE = (
    Path(__file__).parents[2]
    / "artifacts/clifford_t_trace.jsonl"
)


def probability_text(probability: dict) -> str:
    integer = probability["integer"]
    sqrt2 = probability["sqrt2_coefficient"]
    power = probability["denominator_power2"]
    sign = "+" if sqrt2 >= 0 else "-"
    return f"({integer} {sign} {abs(sqrt2)}*sqrt(2)) / 2^{power}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render an exact Clifford+T validation trace."
    )
    parser.add_argument("trace", nargs="?", type=Path, default=DEFAULT_TRACE)
    args = parser.parse_args()
    try:
        with args.trace.open(encoding="utf-8") as stream:
            records = [json.loads(line) for line in stream if line.strip()]
        exact = {
            item["name"]: item
            for item in records
            if item.get("type") == "exact_oracle"
        }
        counterexample = next(
            item for item in records if item.get("type") == "counterexample"
        )
        sampling = next(
            item for item in records if item.get("type") == "sampling"
        )
        routing = next(
            item for item in records if item.get("type") == "routing"
        )
        safety = next(
            item for item in records if item.get("type") == "safety_case"
        )
        for name in ("t_plus_z_basis", "t_plus_x_basis"):
            if name not in exact:
                raise ValueError(f"trace is missing oracle {name}")
    except (OSError, ValueError, StopIteration, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2

    print("Exact Clifford+T reference\n")
    for name in ("t_plus_z_basis", "t_plus_x_basis"):
        oracle = exact[name]
        zero = probability_text(oracle["probability_0"])
        one = probability_text(oracle["probability_1"])
        print(f"{name:16} P(0)={zero}, P(1)={one}")
    print(
        "\nProbability-only injection: "
        f"coherent={probability_text(counterexample['coherent_x_probability'])}, "
        "mixture="
        f"{probability_text(counterexample['incoherent_mixture_x_probability'])}"
    )
    print(
        f"Sampled Z={sampling['z_counts']}, X={sampling['x_counts']} "
        f"with {sampling['fixed_point_bits']}-bit threshold"
    )
    print(
        f"Routing accepted={routing['accepted']}, "
        f"exhaust={routing['external_exhaust']}"
    )
    print(
        f"Saturation accepted={safety['accepted']}, "
        f"exhaust={safety['external_exhaust']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
