# Silicon thermal routing

This adapter represents one CPU heat source, one thermal-interface-material
route, and four lateral silicon routes. Heat is an integer payload. Route
headroom is the maximum heat that can leave through that destination during
one transaction.

The `3 x 3 x 2` periodic grid places the CPU at `(1,1,0)`. Both `z` faces
reach the TIM node at `(1,1,1)`. The four `x` and `y` faces reach separate
lateral nodes. Integer route weights produce the requested split without
continuous Fourier conduction.

Three independent transactions are checked:

| State | TIM capacity | Submitted | TIM | Lateral total | CPU retained | Exhaust |
|---|---:|---:|---:|---:|---:|---:|
| optimal cooling | 8,000 | 10,000 | 6,000 | 4,000 | 0 | 0 |
| TIM degradation | 2,000 | 6,000 | 2,000 | 4,000 | 4,000 | 0 |
| controller failure | 2,000 | 10,000 | 2,000 | 4,000 | 0 | 4,000 |

The degraded controller admits only the 6,000 units that the cooling routes
can carry. The remaining 4,000 stays at the CPU source and triggers
`thermal_throttle_event`. The failed-controller transaction bypasses that
admission step. Its 4,000 rejected units enter external exhaust and are not
interpreted as retained heat.

The adapter does not calculate temperature. Converting retained heat into a
temperature change requires heat capacity, material volume, and a time unit.
It also does not implement Fourier conduction, phonon dispersion, radiation,
convection, or a transistor-scale silicon model.

Run the adapter and renderer:

```bash
./build/tensorless_silicon_thermal_adapter \
  > core/artifacts/silicon_thermal_trace.jsonl
python3 core/adapters/thermal/render_trace.py
```
