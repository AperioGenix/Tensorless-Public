# Full-adder signal validation

This adapter evaluates all eight rows of a one-bit full-adder truth table. The
adapter computes `n = A + B + carry_in`, `sum = n mod 2`, and
`carry = floor(n / 2)`.

Each input bit contributes 1,000 conserved signal subunits. A set sum bit
routes 1,000 units to the sum lane. A set carry bit routes 2,000 units to the
carry lane because it represents two input quanta. Input and output payload
therefore match for every truth-table row.

The core executes the adapter's output command through two physical routes
with zero exhaust and zero conservation residual. A separate invalid-gate
case gives the source 2,000 units and requests 3,000 units of output.
`Tensorless_FpmSandboxStep` must reject that command with
`TENSORLESS_STATUS_STATE_ERROR`.

The core does not compute XOR, AND, or the truth table. This adapter is not a
logic synthesizer, transistor model, SPICE replacement, timing analyzer, or
electronic-design-automation system. It validates conserved output signals
calculated by the adapter.

Run the adapter and renderer:

```bash
./build/tensorless_full_adder_validator \
  > core/artifacts/full_adder_trace.jsonl
python3 core/adapters/digital_logic/render_trace.py
```
