# Seeded XOR tile assembly

This adapter executes a bounded instance of the abstract Tile Assembly Model
on a 4x4 lattice. It represents a discrete self-assembly protocol, not
molecular dynamics.

Each tile reads one-bit glues from its west and south neighbors. Its north and
east output glue is `west XOR south`. Boundary seed glues determine the
assembly. Four tile types are indexed by their input pair:

| Type | West | South | Output |
|---:|---:|---:|---:|
| 0 | 0 | 0 | 0 |
| 1 | 0 | 1 | 1 |
| 2 | 1 | 0 | 1 |
| 3 | 1 | 1 | 0 |

The adapter owns glue compatibility, tile identity, inventory, activation
tokens, frontier order, and lattice state. It submits only compatible
frontier placements. Tensorless admits those requests through independent
capacity-1 lanes and checks conservation.

## Assembly cases

| Case | Initial inventory `00/01/10/11` | Tokens | Filled sites | Rows |
|---|---:|---:|---:|---|
| complete | `0/6/5/5` | 16 | 16 | `1101/0110/1011/1101` |
| missing one `01` tile | `0/5/5/5` | 16 | 15 | `1101/0110/1011/110.` |
| token limit | `0/6/5/5` | 10 | 10 | `1101/011./10../1...` |

Unavailable tiles and tokens remain upstream. They are not submitted to the
core and do not become exhaust. Every run verifies:

```text
tokens consumed = sites filled
initial tokens = tokens consumed + tokens remaining
core accepted placements = sites filled
```

## Capacity safety cases

The duplicate-site case reserves two placement tokens for two compatible
proposals targeting one capacity-1 site. Tensorless accepts one proposal. The
adapter commits one token and releases the other reservation.

The overwrite case submits one proposal to a site with zero remaining
capacity. Tensorless rejects it, the adapter releases its token reservation,
and the existing tile is unchanged.

`invalid_proposal_exhaust` is rejected transaction payload. It is not a
destroyed molecule, physical mass loss, informational energy, or a Landauer
measurement.

This adapter does not model binding rates, temperature, solvent, free energy,
reversible detachment, continuous geometry, or pathway optimization. The
fixed XOR rule is an execution oracle rather than a biological prediction.

Run the adapter and renderer:

```bash
./build/tensorless_molecular_assembly_adapter \
  > core/artifacts/molecular_tile_assembly_trace.jsonl
python3 core/adapters/molecular_assembly/render_trace.py
```
