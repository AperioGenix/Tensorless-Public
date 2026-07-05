# IBM quantum-hardware result adapter

This adapter submits one `H,T,H` measurement circuit to IBM Quantum hardware,
preserves the returned job metadata, and passes the two integer outcome counts
to Tensorless for routing.

It has two isolated parts:

- `ibm_runtime.py` prepares the OpenQASM request, optionally submits one
  SamplerV2 job, and validates the canonical result schema;
- `hardware_result_adapter.cpp` compares the returned histogram with the exact
  Clifford+T Q32 reference, computes deterministic distance metrics, and routes
  the observed counts through Tensorless.

The C++ core has no Qiskit dependency. CTest uses
`fixtures/synthetic_ibm_result.json`; that file is not a hardware result.

## Offline preparation

This command writes the logical OpenQASM and request manifest. It does not
contact IBM:

```bash
python3 core/adapters/quantum_hardware/ibm_runtime.py prepare \
  --output /tmp/qpu_request.json \
  --qasm-output /tmp/qpu_request.qasm \
  --shots 4096
```

## IBM setup

Create a Python environment outside the C++ build and install the provider
packages:

```bash
python3 -m venv .venv-qpu
source .venv-qpu/bin/activate
python -m pip install -r core/adapters/quantum_hardware/requirements.txt
```

Save the IBM token in Qiskit's local account store without putting it in shell
history:

```bash
python - <<'PY'
from getpass import getpass
from qiskit_ibm_runtime import QiskitRuntimeService
QiskitRuntimeService.save_account(
    token=getpass("IBM Quantum token: "),
    name="tensorless-qpu",
)
PY
```

IBM's Open Plan currently provides up to 10 QPU minutes per rolling 28-day
window. Open Plan jobs use job or batch mode, not session mode. Check the
[IBM instance documentation](https://quantum.cloud.ibm.com/docs/en/guides/instances)
before submitting because plan limits can change.

## Hardware submission

Submission consumes QPU quota and therefore requires the explicit
`--confirm-hardware` flag:

```bash
python core/adapters/quantum_hardware/ibm_runtime.py submit \
  --output /tmp/ibm_qpu_result.json \
  --shots 4096 \
  --account tensorless-qpu \
  --confirm-hardware
```

Pass `--backend BACKEND_NAME` to select a backend. Without it, the script asks
IBM for the least-busy operational non-simulator backend.

After the job completes:

```bash
python core/adapters/quantum_hardware/ibm_runtime.py validate \
  /tmp/ibm_qpu_result.json
./build/tensorless_qpu_result_adapter \
  /tmp/ibm_qpu_result.json \
  > /tmp/ibm_qpu_audit.jsonl
python core/adapters/quantum_hardware/render_trace.py \
  /tmp/ibm_qpu_audit.jsonl
```

The canonical result retains backend, job ID, timestamps, circuit hashes,
transpiled ISA gate counts, optimization level, calibration timestamp when
available, package versions, and suppression settings.

## Metric and physics boundary

The adapter reports signed count residuals, L1 count distance, total variation
distance, and Hellinger distance. TV and Hellinger use a Q32 scale.
Square-root operations use deterministic integer flooring.

Histogram differences are not joules, decoherence, or thermal loss.
Tensorless external exhaust only records rejection caused by a configured
detector capacity. The adapter does not infer a hardware noise mechanism from
measurement counts.

## Recorded hardware run

On 2026-07-04 UTC, job `d948sivu62ks7395nee0` ran 4,096 shots on the
156-qubit `ibm_marrakesh` backend. The transpiled circuit contained three
`rz` gates, two `sx` gates, and one measurement. IBM returned 3,465 zero
outcomes and 631 one outcomes.

The exact Clifford+T probability rounds to 3,496 and 600 counts at this shot
depth. The observed total variation distance was `0.00760612`; Hellinger
distance was `0.00752656`. A two-sided binomial test against
`(2+sqrt(2))/4` returned `p=0.17064644`. The ideal probability lies inside
the observed 95% exact binomial interval, `[0.83452361, 0.85687507]`.
This run is statistically compatible with the ideal distribution.

The token-free provider response is stored in
`core/artifacts/ibm_qpu_result.json`. The corresponding Tensorless audit is
`core/artifacts/ibm_qpu_audit.jsonl`.

## Physical identity-depth sweep

The `sweep` command submits several logically equivalent circuits in one
Sampler job. Each identity cycle contains four physical `SX` gates.
`SX^4 = I`, and optimization level 0 prevents the transpiler from removing
the repeated pulses:

```bash
python core/adapters/quantum_hardware/ibm_runtime.py sweep \
  --output-dir core/artifacts/ibm_qpu_depth_sweep \
  --shots 4096 \
  --backend ibm_marrakesh \
  --account tensorless-qpu \
  --cycles 0 8 32 128 \
  --confirm-hardware
```

The recorded run produced:

| Identity cycles | `SX` gates | Counts | TVD | Binomial p-value |
|---:|---:|---:|---:|---:|
| 0 | 2 | 3499 / 597 | 0.00069466 | 0.92956 |
| 8 | 34 | 3524 / 572 | 0.00679817 | 0.22424 |
| 32 | 130 | 3517 / 579 | 0.00508919 | 0.36498 |
| 128 | 514 | 3338 / 758 | 0.03861198 | 1.27e-11 |

The 514-`SX` result is inconsistent with the ideal Bernoulli model for this
recorded run. The four distances are not monotonic, and the experiment does
not identify which physical error mechanism caused the endpoint deviation.
Tensorless accepted every returned count; detector-capacity exhaust remained
zero.

Provider results are under `core/artifacts/ibm_qpu_depth_sweep/`. The combined
Tensorless output is `core/artifacts/ibm_qpu_depth_sweep_audit.jsonl`, and the
separate statistical calculations are in
`core/artifacts/ibm_qpu_depth_sweep_statistics.json`.
