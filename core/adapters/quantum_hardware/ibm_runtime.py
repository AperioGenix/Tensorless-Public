#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


SCHEMA_VERSION = 1
CIRCUIT_NAME = "h_t_h_measure"
SWEEP_CIRCUIT_NAME = "h_t_h_sx4_identity"
LOGICAL_QASM = """OPENQASM 3.0;
include "stdgates.inc";
bit[1] meas;
qubit[1] q;
h q[0];
t q[0];
h q[0];
meas[0] = measure q[0];
"""


def sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def validate_result(document: dict) -> None:
    required = {
        "schema_version",
        "provider",
        "backend",
        "job_id",
        "circuit",
        "logical_circuit_sha256",
        "transpiled_circuit_sha256",
        "submitted_at_utc",
        "completed_at_utc",
        "calibration_timestamp_utc",
        "shots",
        "counts",
        "optimization_level",
        "isa_gate_counts",
        "dynamical_decoupling_enabled",
        "twirling_enabled",
        "synthetic",
        "qiskit_version",
        "qiskit_ibm_runtime_version",
    }
    missing = sorted(required - document.keys())
    if missing:
        raise ValueError(f"missing fields: {', '.join(missing)}")
    if document["schema_version"] != SCHEMA_VERSION:
        raise ValueError("unsupported schema_version")
    if document["provider"] != "ibm_quantum":
        raise ValueError("provider must be ibm_quantum")
    if document["circuit"] not in {CIRCUIT_NAME, SWEEP_CIRCUIT_NAME}:
        raise ValueError("unsupported circuit")
    if document["circuit"] == SWEEP_CIRCUIT_NAME:
        cycles = document.get("identity_cycles")
        if not isinstance(cycles, int) or cycles < 0:
            raise ValueError("identity_cycles must be a non-negative integer")
    if not isinstance(document["shots"], int) or not 0 < document["shots"] <= 1_000_000:
        raise ValueError("shots must be an integer in [1, 1000000]")
    counts = document["counts"]
    if set(counts) != {"0", "1"}:
        raise ValueError("counts must contain only outcomes 0 and 1")
    if any(not isinstance(value, int) or value < 0 for value in counts.values()):
        raise ValueError("counts must be non-negative integers")
    if sum(counts.values()) != document["shots"]:
        raise ValueError("counts must sum to shots")
    for field in ("logical_circuit_sha256", "transpiled_circuit_sha256"):
        value = document[field]
        if (
            not isinstance(value, str)
            or len(value) != 64
            or any(character not in "0123456789abcdef" for character in value)
        ):
            raise ValueError(f"{field} must be a lowercase SHA-256 digest")
    if not isinstance(document["isa_gate_counts"], dict):
        raise ValueError("isa_gate_counts must be an object")
    for field in (
        "dynamical_decoupling_enabled",
        "twirling_enabled",
        "synthetic",
    ):
        if not isinstance(document[field], bool):
            raise ValueError(f"{field} must be boolean")


def write_json(path: Path, document: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def prepare(output: Path, qasm_output: Path, shots: int) -> None:
    qasm_output.parent.mkdir(parents=True, exist_ok=True)
    qasm_output.write_text(LOGICAL_QASM, encoding="utf-8")
    request = {
        "schema_version": SCHEMA_VERSION,
        "provider": "ibm_quantum",
        "circuit": CIRCUIT_NAME,
        "logical_circuit_sha256": sha256_text(LOGICAL_QASM),
        "shots": shots,
        "execution_mode": "job",
        "optimization_level": 1,
        "dynamical_decoupling_enabled": False,
        "twirling_enabled": False,
        "submits_hardware_job": False,
    }
    write_json(output, request)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def calibration_timestamp(backend) -> str | None:
    try:
        properties = backend.properties()
        value = properties.last_update_date
    except (AttributeError, TypeError):
        return None
    if value is None:
        return None
    if value.tzinfo is None:
        value = value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def runtime_imports():
    try:
        from qiskit import (
            ClassicalRegister,
            QuantumCircuit,
            QuantumRegister,
            qasm3,
        )
        from qiskit.transpiler import generate_preset_pass_manager
        from qiskit_ibm_runtime import QiskitRuntimeService, SamplerV2
    except ImportError as error:
        raise RuntimeError(
            "Install qiskit>=2.4,<2.6 and qiskit-ibm-runtime>=0.46,<0.48 "
            "in a separate Python environment."
        ) from error
    return (
        ClassicalRegister,
        QuantumCircuit,
        QuantumRegister,
        qasm3,
        generate_preset_pass_manager,
        QiskitRuntimeService,
        SamplerV2,
    )


def runtime_service(service_type, account_name: str | None):
    return (
        service_type(name=account_name)
        if account_name
        else service_type()
    )


def select_backend(service, backend_name: str | None):
    if backend_name:
        return service.backend(backend_name)
    return service.least_busy(
        operational=True,
        simulator=False,
        min_num_qubits=1,
    )


def make_circuit(
    classical_register_type,
    circuit_type,
    quantum_register_type,
    identity_cycles: int,
):
    qubits = quantum_register_type(1, "q")
    measurements = classical_register_type(1, "meas")
    circuit = circuit_type(qubits, measurements)
    circuit.h(0)
    circuit.t(0)
    for _ in range(identity_cycles):
        for _ in range(4):
            circuit.sx(0)
    circuit.h(0)
    circuit.measure(0, 0)
    return circuit


def submit(
    output: Path,
    shots: int,
    backend_name: str | None,
    account_name: str | None,
) -> None:
    (
        ClassicalRegister,
        QuantumCircuit,
        QuantumRegister,
        qasm3,
        generate_preset_pass_manager,
        QiskitRuntimeService,
        SamplerV2,
    ) = runtime_imports()
    service = runtime_service(QiskitRuntimeService, account_name)
    backend = select_backend(service, backend_name)
    circuit = make_circuit(
        ClassicalRegister,
        QuantumCircuit,
        QuantumRegister,
        0,
    )
    optimization_level = 1
    pass_manager = generate_preset_pass_manager(
        optimization_level=optimization_level,
        backend=backend,
    )
    isa_circuit = pass_manager.run(circuit)
    transpiled_qasm = qasm3.dumps(isa_circuit)
    sampler = SamplerV2(mode=backend)
    sampler.options.dynamical_decoupling.enable = False
    sampler.options.twirling.enable_gates = False
    sampler.options.twirling.enable_measure = False
    submitted_at = utc_now()
    job = sampler.run([isa_circuit], shots=shots)
    result = job.result()
    completed_at = utc_now()
    counts = result[0].data.meas.get_counts()
    canonical_counts = {
        "0": int(counts.get("0", 0)),
        "1": int(counts.get("1", 0)),
    }
    document = {
        "schema_version": SCHEMA_VERSION,
        "provider": "ibm_quantum",
        "backend": backend.name,
        "job_id": job.job_id(),
        "circuit": CIRCUIT_NAME,
        "logical_circuit_sha256": sha256_text(LOGICAL_QASM),
        "transpiled_circuit_sha256": sha256_text(transpiled_qasm),
        "submitted_at_utc": submitted_at,
        "completed_at_utc": completed_at,
        "calibration_timestamp_utc": calibration_timestamp(backend),
        "shots": shots,
        "counts": canonical_counts,
        "optimization_level": optimization_level,
        "isa_gate_counts": {
            str(name): int(count)
            for name, count in isa_circuit.count_ops().items()
        },
        "dynamical_decoupling_enabled": False,
        "twirling_enabled": False,
        "synthetic": False,
        "qiskit_version": importlib.metadata.version("qiskit"),
        "qiskit_ibm_runtime_version": importlib.metadata.version(
            "qiskit-ibm-runtime"
        ),
    }
    validate_result(document)
    write_json(output, document)


def submit_sweep(
    output_dir: Path,
    shots: int,
    backend_name: str | None,
    account_name: str | None,
    identity_cycles: list[int],
) -> None:
    (
        ClassicalRegister,
        QuantumCircuit,
        QuantumRegister,
        qasm3,
        generate_preset_pass_manager,
        QiskitRuntimeService,
        SamplerV2,
    ) = runtime_imports()
    service = runtime_service(QiskitRuntimeService, account_name)
    backend = select_backend(service, backend_name)
    optimization_level = 0
    pass_manager = generate_preset_pass_manager(
        optimization_level=optimization_level,
        backend=backend,
    )
    logical_circuits = [
        make_circuit(
            ClassicalRegister,
            QuantumCircuit,
            QuantumRegister,
            cycles,
        )
        for cycles in identity_cycles
    ]
    isa_circuits = [
        pass_manager.run(circuit) for circuit in logical_circuits
    ]
    for cycles, isa_circuit in zip(identity_cycles, isa_circuits):
        expected_sx = 2 + 4 * cycles
        if int(isa_circuit.count_ops().get("sx", 0)) != expected_sx:
            raise RuntimeError(
                f"transpiler did not preserve the {cycles} identity cycles"
            )

    sampler = SamplerV2(mode=backend)
    sampler.options.dynamical_decoupling.enable = False
    sampler.options.twirling.enable_gates = False
    sampler.options.twirling.enable_measure = False
    submitted_at = utc_now()
    job = sampler.run(isa_circuits, shots=shots)
    result = job.result()
    completed_at = utc_now()
    output_dir.mkdir(parents=True, exist_ok=True)
    result_files = []
    for index, (cycles, logical, isa, pub_result) in enumerate(
        zip(identity_cycles, logical_circuits, isa_circuits, result)
    ):
        counts = pub_result.data.meas.get_counts()
        document = {
            "schema_version": SCHEMA_VERSION,
            "provider": "ibm_quantum",
            "backend": backend.name,
            "job_id": job.job_id(),
            "pub_index": index,
            "circuit": SWEEP_CIRCUIT_NAME,
            "identity_cycles": cycles,
            "identity_definition": "sx^4",
            "logical_circuit_sha256": sha256_text(qasm3.dumps(logical)),
            "transpiled_circuit_sha256": sha256_text(qasm3.dumps(isa)),
            "submitted_at_utc": submitted_at,
            "completed_at_utc": completed_at,
            "calibration_timestamp_utc": calibration_timestamp(backend),
            "shots": shots,
            "counts": {
                "0": int(counts.get("0", 0)),
                "1": int(counts.get("1", 0)),
            },
            "optimization_level": optimization_level,
            "isa_gate_counts": {
                str(name): int(count)
                for name, count in isa.count_ops().items()
            },
            "dynamical_decoupling_enabled": False,
            "twirling_enabled": False,
            "synthetic": False,
            "qiskit_version": importlib.metadata.version("qiskit"),
            "qiskit_ibm_runtime_version": importlib.metadata.version(
                "qiskit-ibm-runtime"
            ),
        }
        validate_result(document)
        filename = f"depth_{cycles:03d}.json"
        write_json(output_dir / filename, document)
        result_files.append(filename)
    write_json(
        output_dir / "manifest.json",
        {
            "provider": "ibm_quantum",
            "backend": backend.name,
            "job_id": job.job_id(),
            "shots_per_circuit": shots,
            "identity_cycles": identity_cycles,
            "identity_definition": "sx^4",
            "optimization_level": optimization_level,
            "result_files": result_files,
            "submitted_at_utc": submitted_at,
            "completed_at_utc": completed_at,
        },
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare, submit, or validate the IBM QPU experiment."
    )
    commands = parser.add_subparsers(dest="command", required=True)

    prepare_parser = commands.add_parser(
        "prepare",
        help="Write the logical OpenQASM and a request manifest without submitting.",
    )
    prepare_parser.add_argument("--output", required=True, type=Path)
    prepare_parser.add_argument("--qasm-output", required=True, type=Path)
    prepare_parser.add_argument("--shots", type=int, default=4096)

    validate_parser = commands.add_parser(
        "validate",
        help="Validate a canonical QPU result without network access.",
    )
    validate_parser.add_argument("result", type=Path)

    submit_parser = commands.add_parser(
        "submit",
        help="Submit one job to IBM Quantum hardware.",
    )
    submit_parser.add_argument("--output", required=True, type=Path)
    submit_parser.add_argument("--shots", type=int, default=4096)
    submit_parser.add_argument("--backend")
    submit_parser.add_argument(
        "--account",
        help="Name of credentials in Qiskit's local account store.",
    )
    submit_parser.add_argument(
        "--confirm-hardware",
        action="store_true",
        help="Required acknowledgement that this consumes QPU quota.",
    )

    sweep_parser = commands.add_parser(
        "sweep",
        help="Submit one multi-circuit physical SX-depth sweep.",
    )
    sweep_parser.add_argument("--output-dir", required=True, type=Path)
    sweep_parser.add_argument("--shots", type=int, default=4096)
    sweep_parser.add_argument("--backend")
    sweep_parser.add_argument("--account")
    sweep_parser.add_argument(
        "--cycles",
        type=int,
        nargs="+",
        default=[0, 8, 32, 128],
    )
    sweep_parser.add_argument(
        "--confirm-hardware",
        action="store_true",
        help="Required acknowledgement that this consumes QPU quota.",
    )

    args = parser.parse_args()
    try:
        if args.command == "prepare":
            if not 0 < args.shots <= 1_000_000:
                raise ValueError("shots must be in [1, 1000000]")
            prepare(args.output, args.qasm_output, args.shots)
            return 0
        if args.command == "validate":
            document = json.loads(args.result.read_text(encoding="utf-8"))
            validate_result(document)
            print(
                f"valid {document['provider']} result: "
                f"{document['shots']} shots, synthetic={document['synthetic']}"
            )
            return 0
        if not args.confirm_hardware:
            raise ValueError(
                "submit requires --confirm-hardware because it consumes QPU quota"
            )
        if not 0 < args.shots <= 1_000_000:
            raise ValueError("shots must be in [1, 1000000]")
        if args.command == "submit":
            submit(args.output, args.shots, args.backend, args.account)
        else:
            if (
                not args.cycles
                or any(cycles < 0 for cycles in args.cycles)
                or len(set(args.cycles)) != len(args.cycles)
            ):
                raise ValueError(
                    "cycles must be distinct non-negative integers"
                )
            submit_sweep(
                args.output_dir,
                args.shots,
                args.backend,
                args.account,
                args.cycles,
            )
        return 0
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
