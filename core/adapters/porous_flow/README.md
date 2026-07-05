# Finite porous-flow capacity

This adapter represents an injection manifold with two branches. One branch
feeds sandstone with capacity 8,000. The other feeds a micro-fracture with
capacity 2,000 in the safe state and 500 after degradation.

The `3 x 2 x 1` grid uses one lane per branch. Each source routes causally in
the `+x` direction to its sink. The adapter owns the manifold split because
the public sandbox route policy cannot assign independent `+x` and `-x`
weights from one source while suppressing all other faces.

Three independent transactions are checked:

| State | Sandstone | Fracture | Retained at manifold | Exhaust |
|---|---:|---:|---:|---:|
| optimal flow | 8,000 | 2,000 | 0 | 0 |
| permeability degradation | 8,000 | 500 | 1,500 | 0 |
| controller failure | 8,000 | 500 | 0 | 1,500 |

During controlled degradation, 1,500 fluid subunits remain at the
fracture-side manifold source and trigger `wellhead_pressure_buildup`. During
failed control, those 1,500 units enter external exhaust and trigger
`overpressure_boundary_release`. Exhaust is not reinjected or interpreted as
fluid retained in the represented pore volume.

This is not a Darcy-law solver. It does not calculate a pressure gradient,
viscosity, permeability coefficient, cross-sectional area, or path length. It
also does not create fractures or discover percolation paths. It executes and
checks a finite branch-capacity controller.

Run the adapter and renderer:

```bash
./build/tensorless_porous_flow_adapter \
  > core/artifacts/porous_flow_trace.jsonl
python3 core/adapters/porous_flow/render_trace.py
```
