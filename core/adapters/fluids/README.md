# Periodic transport adapter

This adapter places a 3×3×3 block in a 7×7×7 periodic grid. Every occupied
node holds 1,000 energy subunits and 1,000 units of positive x momentum.
Causal routing sends the full payload and momentum to the positive x neighbor
on each tick.

The oracle is a coordinate translation by one x cell per tick, modulo seven.
After every tick, the adapter checks every node, every momentum channel,
external exhaust, and the conservation residuals.

This test covers periodic translation and momentum accounting. It is not a
driven-cavity solver. The core has no no-slip wall condition, pressure
projection, incompressibility solve, or viscosity operator.

The executable writes one block-position frame per tick followed by a summary.
Render the periodic cross-section from the repository root:

```bash
python3 core/adapters/fluids/render_trace.py
```

The default input is `core/artifacts/transport_trace.jsonl`.
