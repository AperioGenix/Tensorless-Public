# Domain adapters

Adapters assign domain meaning to Tensorless state. They own their run loops,
read core state, compute domain-specific quantities, and submit discrete inputs
through the public API. Core code must not include or call adapter code.

The current adapters are:

- `diffusion/`: six-neighbor classical diffusion against an integer
  path-count oracle;
- `vehicle/`: yaw-targeted rear-wheel force allocation with upstream
  holdback;
- `thermal/`: silicon heat routing, source retention, and failed-controller
  capacity enforcement;
- `porous_flow/`: two-branch fluid capacity control, retained pressure, and
  boundary release;
- `chemistry/`: tagged integer stoichiometry and finite batch admission;
- `protein_lattice/`: HP candidate validation and finite steric packing;
- `digital_logic/`: full-adder truth-table signal conservation;
- `market/`: bounded share execution and integer-cent cash accounting;
- `quantum/`: single-qubit oracles, multi-qubit stabilizer tableaux, a bounded
  exact Clifford+T reference, seeded measurement, and detector capacity;
- `quantum_hardware/`: optional IBM QPU submission, canonical result
  provenance, histogram metrics, and count routing;
- `molecular_assembly/`: seeded XOR tile assembly with finite inventory,
  activation tokens, and capacity-1 sites;
- `fluids/`: exact periodic block translation and momentum accounting;
- `network/`: adapter-owned FIFO queues using Tensorless for per-tick
  capacity arbitration;
- `pathfinding/`: dynamic obstacle replanning with exact one-hop core
  transport.

Each adapter is a self-contained CTest target. Its README states
which equations belong to the adapter and which properties the core checks.
