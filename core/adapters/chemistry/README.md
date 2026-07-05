# Finite stoichiometric batch

This adapter executes the batch reaction `A + B -> AB` as conserved integer
constituent units. Separate reagent sources route into one finite product
sink. Off-diagonal momentum channels retain species identity through target
contention.

Initial inventory is 6,000 A units and 4,000 B units. Three independent
batches are checked:

| State | Accepted A/B | Retained A/B | Exhaust A/B | AB pairs |
|---|---:|---:|---:|---:|
| safe batch | 3,000 / 3,000 | 3,000 / 1,000 | 0 / 0 | 3,000 |
| catalyst degradation | 1,000 / 1,000 | 5,000 / 3,000 | 0 / 0 | 1,000 |
| failed admission | 1,000 / 1,000 | 3,000 / 1,000 | 2,000 / 2,000 | 1,000 |

The adapter owns stoichiometric pairing and batch admission. During catalyst
degradation it submits only the 1,000 valid pairs that fit. The failed
admission transaction submits the old 3,000-pair command into 2,000
constituent units of product headroom. Largest-remainder contention accepts
1,000 units of each species and records 2,000 units of each in external
exhaust.

This is not a chemical kinetics model. It does not calculate reaction rates,
concentration, activation energy, temperature, equilibrium, or transition
states. A fresh sandbox represents each finite batch.

Run the adapter and renderer:

```bash
./build/tensorless_stoichiometric_batch_adapter \
  > core/artifacts/stoichiometric_batch_trace.jsonl
python3 core/adapters/chemistry/render_trace.py
```
