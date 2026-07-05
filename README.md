<div align="center">
  <h1>⚙️ Tensorless Engine</h1>
  <p><strong>An exact thermodynamic execution sandbox.</strong></p>
  <p><i>Reduces continuous physical domains to deterministic integer routing via a strict C ABI.</i></p>
  <br/>
</div>

As a C++20 execution engine, Tensorless operates on bounded, discrete routing mechanics across a periodic three-dimensional integer grid.

> **Core = mechanics. Adapter = meaning.**

---

## 🏛️ The Architectural Law

- **State Evolution**: The core library advances state, routing, scheduling, transport, coarse-graining, conservation ledgers, and diagnostic measurements.
- **Strict Agnosticism**: The core *never* decides what the state means.
- **Meaning Assignment**: Domain adapters assign meaning to the bounded execution rules, translating them into diffusion, markets, quantum circuits, thermal routing, network queues, pathfinding, or other domains.
- **Unidirectional Dependency**: The dependency direction is strictly one-way: adapters link to the public ABI, and the core implementation remains entirely domain-blind.
- **Zero External Baggage**: Tensorless does not depend on an external tensor framework, numerical runtime, plugin system, or domain-specific core branch.
- **Purity Enforcement**: The core does not link to adapters, call adapter callbacks, load plugins, inspect domain names, or branch on domain-specific concepts.

---

## 🛑 The Landauer Constraint

- **Mandatory Erasure**: All state transformations require discrete erasure.
- **Physical Bounds**: The engine enforces the $kT \ln 2$ lower bound on bit erasure directly at the routing layer.
- **Mathematical Starvation**: If a routing adapter attempts to execute an operation without sufficient subunits to pay the required entropic toll, the operation mathematically starves and the trace faults.
- **Subunit Exactness**: Both energy and action use exact one-third-micro-action subunits, and successful ticks verify energy and momentum conservation identities.
- **Ledger Segregation**: Accepted and rejected momentum are accounted separately, while external exhaust and starvation have distinct ledgers.

---

## 🛡️ Epistemic Boundary and Non-Claims

Tensorless strictly distinguishes implemented mechanics from external interpretation. A passing test suite establishes agreement with the implemented contracts, such as enforcing capacity ceilings and energy conservation identities.

**It does *not* establish:**
- Empirical validity of Finite Possibility Mechanics.
- Correctness of an adapter’s external model.
- Universal physical equivalence between unrelated domains.

> ⚠️ **Note:** A zero conservation residual validates the represented ledger identity. It does not validate an external domain interpretation.

---

## 🔍 The Audit Trail

To prevent "hidden tuning" attacks, the `TensorlessParamLedger` records every run parameter with its classification (`FIXED`, `DERIVED`, `FREE`, or `FITTED`), source, and value. The ledger computes a non-cryptographic **FNV-1a fingerprint** over the declared entries for drift detection, ensuring that any changes to core constants or adapter variables leave a traceable record.

---

## 🛠️ Usage and Validation

This public repository serves as the API reference and validation harness. The private core implementation is strictly black-boxed and excluded. **You cannot modify the core thermodynamic ledger; you can only route through it.**

To validate the thermodynamic bounds locally, you must download the pre-compiled FPM binaries:

**1. Download and extract the latest release**
Download the `.zip` for your platform (Linux, macOS, or Windows) from the [Releases page](https://github.com/AperioGenix/Tensorless-Public/releases). Extract the archive and open a terminal in the extracted folder.

**2. Generate the build files**
```bash
# Targets the 'source' folder bundled in the zip release
cmake -S source -B build -DCMAKE_BUILD_TYPE=Release
```

**3. Compile the adapters**
```bash
cmake --build build --parallel
```

**4. Run the exact accounting experiments**
```bash
ctest --test-dir build --output-on-failure
```

**5. Run specific validators directly**
```bash
# On Windows, this will be build/Release/tensorless_landauer_erasure_validator.exe
./build/tensorless_landauer_erasure_validator
```
