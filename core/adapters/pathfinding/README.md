# Dynamic pathfinding adapter

This adapter routes one integer payload through a 15 by 9 logical maze. The
logical map is a two-dimensional slice of a 15 by 9 by 1 Tensorless sandbox.
The adapter forbids periodic wraparound and permits four-neighbor moves.

The adapter owns pathfinding. It computes a reverse breadth-first distance
field from the goal and selects the next open neighbor using a fixed order.
At tick 4 it closes the next cell on the initial route, rebuilds the distance
field, and follows a different shortest route to the goal.

Each hop is one fresh sandbox transaction. The source node receives one energy
subunit and one signed diagonal momentum unit for the chosen x or y direction.
The adapter then checks every node, all nine momentum channels, conservation
residuals, external exhaust, and starvation. The transaction passes only when
the core moves the unit to the selected adjacent cell.

A separate forward breadth-first search reconstructs shortest paths using a
different neighbor order and separate state. It checks the initial path length
and the remaining length after the obstacle change. A wall spanning the
interior supplies an unreachable case. No sandbox transaction is submitted
when that oracle reports no path.

The executable writes one JSON object per hop followed by a summary object.
The JSON Lines trace can be consumed by a renderer, but this target has no
graphics or video dependency.

Render the bundled trace from the repository root:

```bash
python3 core/adapters/pathfinding/render_trace.py
```

Tensorless does not calculate the path, inspect obstacles, or replace A*.
It executes the adapter-selected integer movement and accounts for its energy
and momentum. The adapter and the core have separate responsibilities.
