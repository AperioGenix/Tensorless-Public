# Active rear-axle torque vectoring

This adapter computes a rear-wheel force split from total drive demand, a yaw
moment target, tire limits, and half-track length. It then asks Tensorless to
execute the controller command as one closed transport transaction.

## Tire capacity

The adapter owns the tire calculation. For each driven wheel it computes

```text
Fx_capacity = integer_sqrt((mu_Fz)^2 - Fy^2)
```

The test uses exact 3-4-5 triples:

| Wheel | `mu_Fz` | `Fy` | `Fx_capacity` |
|---|---:|---:|---:|
| rear left, inside | 5,000 | 4,000 | 3,000 |
| rear right, outside | 15,000 | 12,000 | 9,000 |

## Controller

For half-track `h`, rear longitudinal forces produce

```text
Mz = h * (Fx_right - Fx_left)
```

The finite test supplies:

```text
total force demand = 14,000
yaw moment target = 6,000
half-track = 1
```

Without tire limits, the requested split would be 4,000 left and 10,000
right. Both values exceed their wheel limits.

The controller preserves the requested force difference while finding the
largest admissible total:

```text
d = Mz / h
A = min(total demand, 2 * left capacity + d,
                      2 * right capacity - d)
Fx_left  = (A - d) / 2
Fx_right = (A + d) / 2
```

All divisions must be exact in the integer unit system. The result is 3,000
left, 9,000 right, and 2,000 held upstream by traction control.

## Core transaction

One engine-storage node starts with 14,000 force-equivalent payload subunits.
The adapter injects the admitted 12,000. Two route weights split that payload
3,000:9,000 toward separate tire nodes. The engine node retains 2,000, both
tire commands arrive exactly, and external exhaust remains zero.

The independent oracle contains the closed-form expected split. It does not
call the controller or Tensorless. The executable also checks the achieved
yaw moment, tire capacities, retained engine amount, conservation residuals,
and deterministic replay.

## Failed-controller safety case

A second transaction bypasses the controller and submits a 7,000:7,000 split
from the same 14,000-unit source. The rear-left tire accepts 3,000 and rejects
4,000 at its capacity boundary. The rear-right tire accepts all 7,000. The
core records the rejected 4,000 in external exhaust with zero conservation
residual.

The failed-controller transaction is not fed back into the normal controller
run. It exists to check that the core enforces the tire-node capacities when
an upstream command is invalid.

This is a finite rear-axle controller test, not a full vehicle model. It does
not derive normal load, friction coefficient, lateral force, steering demand,
slip ratio, slip angle, tire temperature, or Pacejka coefficients. Those are
adapter inputs. The payload uses longitudinal-force subunits after drivetrain
conversion; the core does not perform engine-torque or wheel-radius math.

Run the adapter and save its JSON Lines trace:

```bash
./build/tensorless_active_torque_vectoring_adapter \
  > core/artifacts/active_torque_vectoring_trace.jsonl
```

Render the saved trace:

```bash
python3 core/adapters/vehicle/render_trace.py
```
