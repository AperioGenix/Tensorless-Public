# Single-qubit stabilizer circuit validation

This adapter represents the exact stabilizer states `Z+`, `Z-`, `X+`, and
`X-` as a finite enum. It implements the single-qubit Clifford gates `H`, `X`,
and `Z` as exact state transitions.

Three circuits begin in `Z+`, which represents `|0>`:

| Gates | Final state | Z measurement weights |
|---|---|---:|
| `H,H` | `Z+` | 10,000 / 0 |
| `H,Z,H` | `Z-` | 0 / 10,000 |
| `H` | `X+` | 5,000 / 5,000 |

The `H,Z,H` circuit retains the phase distinction between `X+` and `X-` and
therefore produces the opposite deterministic detector result from `H,H`.
The adapter owns stabilizer evolution and exact Born branch weights.
Tensorless routes the resulting 10,000 shot-count subunits to two detector
sinks and checks conservation.

A separate readout-saturation transaction sends the `H` circuit's
5,000 / 5,000 weights to detectors with capacity 2,000 each. The core accepts
2,000 at each detector and records 6,000 in external exhaust. This is detector
saturation, not decoherence or wavefunction collapse.

This adapter is not a general quantum simulator. It does not implement
arbitrary amplitudes, non-Clifford gates, entanglement, density matrices,
noise, or continuous Schrödinger evolution. (See the Landauer erasure validator below for thermodynamic bounds).

## Landauer Erasure Validator

`landauer_erasure.cpp` implements a strict thermodynamic bounding check based on Landauer's principle. It tests the engine's ability to enforce physical friction during information routing (e.g., erasing or sending a quantum payload).

The validator scales the room-temperature Landauer limit ($kT \\ln 2$) into exactly `2805` engine action subunits. It sets up two safety cases:
1. **Sufficient Energy**: A source node has enough energy to pay the 2805 subunit route cost. The payload arrives successfully.
2. **Starvation Deficit**: A source node attempts to route a payload but cannot afford the 2805 subunit cost upfront. The engine strictly enforces the First Law of Thermodynamics: the payload is trapped, the node burns its remaining energy trying to pay the cost, the successfully paid fraction is exhausted to the external environment, and no free energy is created in the grid.

## Multi-qubit stabilizer tableau

`stabilizer_tableau.cpp` adds a binary Aaronson-Gottesman-style tableau for
multi-qubit Clifford circuits. The tableau stores destabilizer and stabilizer
generators, tracks Pauli signs during row multiplication, and implements
`H`, `S`, `X`, `Z`, and `CNOT`.

Each sampled shot copies the post-circuit tableau before destructive
computational-basis measurement. A fixed SplitMix64 seed makes the shot batch
replayable. Qubit 0 is the most-significant output bit.

The validator checks:

- a Bell circuit produces only `00` and `11`;
- applying the inverse Bell circuit returns `00` deterministically;
- adding `Z(0)` before the inverse Bell circuit returns `10`
  deterministically, which checks phase-sign propagation;
- `X(0)` returns `10`, which fixes the output bit order;
- a 64-qubit GHZ circuit produces only the all-zero and all-one strings across
  2,048 sampled shots without allocating a state vector;
- four dedicated Tensorless ingress links transport the sampled two-qubit
  detector counts and conserve accepted plus exhausted subunits;
- detector capacity 2,000 accepts 2,000 for each populated Bell outcome and
  records the remaining 6,000 as external exhaust.

Tableau gate evolution is polynomial in the qubit count. Producing an exact
list of all `2^N` computational-basis probabilities is not. The scalable path
therefore samples measurements with a documented seed. Tensorless does not
compute stabilizer evolution, entanglement, or Born probabilities. It routes
the integer detector counts supplied by the adapter.

## Exact Clifford+T reference

`clifford_t_validator.cpp` represents each amplitude as four integer
coefficients in `Z[w]`, where `w = exp(i*pi/4)` and `w^4 = -1`, plus a shared
power-of-`sqrt(2)` denominator. This supports exact `H`, `S`, `T`, `X`, `Z`,
and `CNOT` evolution without floating-point arithmetic.

The validator checks `T^2 = S`, `T^8 = I`, and an exact Bell-state CNOT
oracle. For `T|+>` it obtains:

- Z basis: `1/2`, `1/2`;
- X basis: `(2+sqrt(2))/4`, `(2-sqrt(2))/4`.

The X-basis result is also compared with an incoherent 50/50 mixture. The
mixture remains 50/50, so a pair of integer readout counts cannot replace the
relative T phase.

Irrational algebraic probabilities cannot be converted into an exact finite
shot split. Sampling uses a documented 32-bit integer threshold derived from
`floor(sqrt(2) * 2^32)`, with probability error below `2^-31`. Tensorless
routes the resulting counts and applies detector capacities in a separate
transaction.

This reference simulator stores `2^N` amplitudes and rejects more than eight
qubits. It is not the stabilizer tableau, a magic-state distillation protocol,
or a replacement for quantum hardware.

Run the adapter and renderer:

```bash
./build/tensorless_stabilizer_circuit_validator \
  > core/artifacts/stabilizer_circuit_trace.jsonl
python3 core/adapters/quantum/render_trace.py

./build/tensorless_stabilizer_tableau_validator \
  > core/artifacts/stabilizer_tableau_trace.jsonl
python3 core/adapters/quantum/render_tableau_trace.py

./build/tensorless_clifford_t_validator \
  > core/artifacts/clifford_t_trace.jsonl
python3 core/adapters/quantum/render_clifford_t_trace.py
```
