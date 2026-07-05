# Tensorless

An exact thermodynamic execution sandbox. Reduces continuous physical domains to deterministic integer routing via a strict C ABI.

This repository provides the C ABI headers, domain validation adapters, and pre-compiled FPM architecture binaries. The engine internals are strictly black-boxed to guarantee physical bounds conservation. You cannot modify the core thermodynamic ledger; you can only route through it.

## The Landauer Constraint

All state transformations require discrete erasure. The engine enforces the $kT \ln 2$ lower bound on bit erasure at the routing layer. If a routing adapter attempts to execute an operation without sufficient subunits to pay the required entropic toll, the operation mathematically starves and the trace faults.

## Usage

Download the latest platform release. The generated `CMakeLists.txt` will automatically link the domain adapters (e.g., fluid dynamics, pathfinding, molecular assembly) to the core `tensorless` binary.

To validate the bounds locally, run the `tensorless_landauer_erasure_validator` target.
