# Discrete diffusion adapter

This adapter runs an eight-tick, six-neighbor classical random walk on a
19×19×19 periodic grid. The initial mass is `6^8`, so every occupied node is
divisible by six before the final tick. This removes rounding from the
comparison.

An independent integer dynamic program computes the path counts. The adapter
fails if any node differs from that oracle by one subunit or if a conservation
residual is nonzero.

This is a classical diffusion test. It does not implement a free-particle
Hamiltonian, complex phase evolution, or quantum wave-packet dispersion.

The executable writes one central-slice frame per tick followed by a summary.
Render the captured trace from the repository root:

```bash
python3 core/adapters/diffusion/render_trace.py
```

The default input is `core/artifacts/diffusion_trace.jsonl`.
