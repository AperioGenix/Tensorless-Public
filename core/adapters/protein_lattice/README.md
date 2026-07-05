# Protein-lattice candidate validation

This adapter checks supplied conformations for the HP lattice sequence
`HHPHHH`. Consecutive residues must have Manhattan distance one, coordinates
must be unique, and each nonconsecutive lattice-adjacent H-H pair contributes
`-1` to the candidate energy.

Three candidates are evaluated:

| Candidate | Connected | Self-avoiding | H-H contacts | Energy |
|---|---|---|---:|---:|
| straight | yes | yes | 0 | 0 |
| folded U | yes | yes | 2 | -2 |
| clashing | yes | no | 0 | invalid |

The adapter prefers the folded U among the two valid supplied candidates. It
does not search the lattice or claim that this is a globally optimal fold.

A separate core transaction stresses finite packing volume. Three tagged
regions submit `[4,000, 3,000, 2,000]` units into capacity `6,000`.
Largest-remainder contention accepts `[2,667, 2,000, 1,333]` and rejects
`[1,333, 1,000, 667]`. Rejected units are invalid packing-proposal volume,
not atoms removed from a folded protein.

This adapter does not model solvent, temperature, folding kinetics, bond
angles, side chains, electrostatics, or continuous molecular geometry. The
core does not discover or score conformations; the adapter owns those rules.

Run the adapter and renderer:

```bash
./build/tensorless_protein_lattice_adapter \
  > core/artifacts/protein_lattice_trace.jsonl
python3 core/adapters/protein_lattice/render_trace.py
```
