#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kExtent = 3;
constexpr uint64_t kShots = 10000;

enum class StabilizerState : uint32_t {
    ZPlus,
    ZMinus,
    XPlus,
    XMinus,
};

enum class Gate : uint32_t {
    H,
    X,
    Z,
};

struct DetectorResult {
    uint64_t detector_zero = 0;
    uint64_t detector_one = 0;
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const DetectorResult&) const = default;
};

struct CircuitResult {
    std::string_view name;
    std::string_view gates;
    StabilizerState final_state = StabilizerState::ZPlus;
    std::array<uint64_t, 2> measurement_weights{};
    DetectorResult detectors{};

    bool operator==(const CircuitResult&) const = default;
};

struct RunResult {
    CircuitResult hh{};
    CircuitResult hzh{};
    CircuitResult h{};
    DetectorResult saturation{};

    bool operator==(const RunResult&) const = default;
};

StabilizerState ApplyGate(
    StabilizerState state,
    Gate gate) {
    switch (gate) {
    case Gate::H:
        switch (state) {
        case StabilizerState::ZPlus:
            return StabilizerState::XPlus;
        case StabilizerState::ZMinus:
            return StabilizerState::XMinus;
        case StabilizerState::XPlus:
            return StabilizerState::ZPlus;
        case StabilizerState::XMinus:
            return StabilizerState::ZMinus;
        }
        break;
    case Gate::X:
        switch (state) {
        case StabilizerState::ZPlus:
            return StabilizerState::ZMinus;
        case StabilizerState::ZMinus:
            return StabilizerState::ZPlus;
        case StabilizerState::XPlus:
            return StabilizerState::XPlus;
        case StabilizerState::XMinus:
            return StabilizerState::XMinus;
        }
        break;
    case Gate::Z:
        switch (state) {
        case StabilizerState::ZPlus:
            return StabilizerState::ZPlus;
        case StabilizerState::ZMinus:
            return StabilizerState::ZMinus;
        case StabilizerState::XPlus:
            return StabilizerState::XMinus;
        case StabilizerState::XMinus:
            return StabilizerState::XPlus;
        }
        break;
    }
    return state;
}

bool CheckGateTable() {
    constexpr std::array<StabilizerState, 4> states{
        StabilizerState::ZPlus,
        StabilizerState::ZMinus,
        StabilizerState::XPlus,
        StabilizerState::XMinus,
    };
    constexpr std::array<Gate, 3> gates{
        Gate::H, Gate::X, Gate::Z};
    constexpr std::array<std::array<StabilizerState, 4>, 3>
        expected{{
            {
                StabilizerState::XPlus,
                StabilizerState::XMinus,
                StabilizerState::ZPlus,
                StabilizerState::ZMinus,
            },
            {
                StabilizerState::ZMinus,
                StabilizerState::ZPlus,
                StabilizerState::XPlus,
                StabilizerState::XMinus,
            },
            {
                StabilizerState::ZPlus,
                StabilizerState::ZMinus,
                StabilizerState::XMinus,
                StabilizerState::XPlus,
            },
        }};
    for (size_t gate = 0; gate < gates.size(); ++gate)
        for (size_t state = 0; state < states.size(); ++state)
            if (ApplyGate(states[state], gates[gate]) !=
                expected[gate][state])
                return false;
    return true;
}

std::array<uint64_t, 2> MeasureZ(
    StabilizerState state) {
    switch (state) {
    case StabilizerState::ZPlus:
        return {kShots, 0};
    case StabilizerState::ZMinus:
        return {0, kShots};
    case StabilizerState::XPlus:
    case StabilizerState::XMinus:
        return {kShots / 2, kShots / 2};
    }
    return {};
}

std::string_view StateName(StabilizerState state) {
    switch (state) {
    case StabilizerState::ZPlus:
        return "Z+";
    case StabilizerState::ZMinus:
        return "Z-";
    case StabilizerState::XPlus:
        return "X+";
    case StabilizerState::XMinus:
        return "X-";
    }
    return "invalid";
}

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(kExtent, 1, x, y, 0);
}

bool ExecuteDetectors(
    const std::array<uint64_t, 2>& weights,
    const std::array<uint64_t, 2>& capacities,
    DetectorResult& result) {
    if (weights[0] + weights[1] != kShots ||
        capacities[0] > tensorless_adapter::kEnergyCeiling ||
        capacities[1] > tensorless_adapter::kEnergyCeiling)
        return false;

    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(
            kExtent, kExtent, 1);
    info.isotropic_face_weight = 0;
    tensorless_adapter::Sandbox sandbox;
    const uint64_t source = Index(1, 1);
    const uint64_t detector_zero = Index(2, 1);
    const uint64_t detector_one = Index(1, 2);
    if (!sandbox.Create(info))
        return false;

    TensorlessFpmSandboxNode source_node{};
    source_node.energy_subunits = kShots;
    source_node.momentum[0][0] =
        static_cast<int64_t>(weights[0]);
    source_node.momentum[1][1] =
        static_cast<int64_t>(weights[1]);
    TensorlessFpmSandboxNode zero_node{};
    zero_node.energy_subunits =
        tensorless_adapter::kEnergyCeiling - capacities[0];
    TensorlessFpmSandboxNode one_node{};
    one_node.energy_subunits =
        tensorless_adapter::kEnergyCeiling - capacities[1];
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), source, &source_node),
            "Tensorless_FpmSandboxSetNode(shot source)") ||
        !tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), detector_zero, &zero_node),
            "Tensorless_FpmSandboxSetNode(detector zero)") ||
        !tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), detector_one, &one_node),
            "Tensorless_FpmSandboxSetNode(detector one)"))
        return false;

    const uint64_t zero_initial = zero_node.energy_subunits;
    const uint64_t one_initial = one_node.energy_subunits;
    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[source] = kShots;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(detectors)"))
        return false;

    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), detector_zero, &zero_node),
            "Tensorless_FpmSandboxGetNode(detector zero)") ||
        !tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), detector_one, &one_node),
            "Tensorless_FpmSandboxGetNode(detector one)"))
        return false;

    result = {};
    result.detector_zero =
        zero_node.energy_subunits - zero_initial;
    result.detector_one =
        one_node.energy_subunits - one_initial;
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];
    return tensorless_adapter::ResidualsAreZero(stats);
}

CircuitResult ExecuteCircuit(
    std::string_view name,
    std::string_view gate_names,
    const std::vector<Gate>& gates) {
    StabilizerState state = StabilizerState::ZPlus;
    for (Gate gate : gates)
        state = ApplyGate(state, gate);
    const std::array<uint64_t, 2> weights = MeasureZ(state);
    CircuitResult result{name, gate_names, state, weights, {}};
    if (!ExecuteDetectors(
            weights, {kShots, kShots}, result.detectors))
        result.name = "error";
    return result;
}

bool Execute(RunResult& result) {
    if (!CheckGateTable())
        return false;
    result = {};
    result.hh = ExecuteCircuit(
        "identity_interference", "H,H",
        {Gate::H, Gate::H});
    result.hzh = ExecuteCircuit(
        "phase_flipped_interference", "H,Z,H",
        {Gate::H, Gate::Z, Gate::H});
    result.h = ExecuteCircuit(
        "x_basis_measurement", "H",
        {Gate::H});
    if (result.hh.name == "error" ||
        result.hzh.name == "error" ||
        result.h.name == "error" ||
        !ExecuteDetectors(
            {kShots / 2, kShots / 2},
            {2000, 2000}, result.saturation))
        return false;

    const CircuitResult hh_oracle{
        "identity_interference", "H,H",
        StabilizerState::ZPlus, {10000, 0},
        {10000, 0, 0, 0, {}}};
    const CircuitResult hzh_oracle{
        "phase_flipped_interference", "H,Z,H",
        StabilizerState::ZMinus, {0, 10000},
        {0, 10000, 0, 0, {}}};
    const CircuitResult h_oracle{
        "x_basis_measurement", "H",
        StabilizerState::XPlus, {5000, 5000},
        {5000, 5000, 0, 0, {}}};
    const DetectorResult saturation_oracle{
        2000, 2000, 6000, 0, {}};
    return result.hh == hh_oracle &&
           result.hzh == hzh_oracle &&
           result.h == h_oracle &&
           result.saturation == saturation_oracle;
}

void WriteCircuit(const CircuitResult& result) {
    std::cout
        << "{\"type\":\"circuit\","
        << "\"name\":\"" << result.name << "\","
        << "\"gates\":\"" << result.gates << "\","
        << "\"initial_state\":\"Z+\","
        << "\"final_state\":\""
        << StateName(result.final_state) << "\","
        << "\"measurement_weights\":["
        << result.measurement_weights[0] << ","
        << result.measurement_weights[1] << "],"
        << "\"detector_0\":"
        << result.detectors.detector_zero << ","
        << "\"detector_1\":"
        << result.detectors.detector_one << ","
        << "\"external_exhaust\":"
        << result.detectors.external_exhaust << ","
        << "\"energy_residual\":"
        << result.detectors.energy_residual << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteCircuit(result.hh);
    WriteCircuit(result.hzh);
    WriteCircuit(result.h);
    std::cout
        << "{\"type\":\"safety_case\","
        << "\"name\":\"detector_saturation\","
        << "\"measurement_weights\":[5000,5000],"
        << "\"detector_capacities\":[2000,2000],"
        << "\"detector_0\":"
        << result.saturation.detector_zero << ","
        << "\"detector_1\":"
        << result.saturation.detector_one << ","
        << "\"external_exhaust\":"
        << result.saturation.external_exhaust << ","
        << "\"detector_saturation\":true,"
        << "\"energy_residual\":"
        << result.saturation.energy_residual << "}\n"
        << "{\"type\":\"summary\","
        << "\"adapter\":\"stabilizer_circuit_validator\","
        << "\"shots\":10000,"
        << "\"gate_set\":\"H,X,Z\","
        << "\"detector_saturation_exhaust\":6000,"
        << "\"oracle_verified\":true,"
        << "\"deterministic_replay\":true}\n";
}
}  // namespace

int main() {
    RunResult first{};
    RunResult replay{};
    if (!Execute(first) || !Execute(replay))
        return 2;
    if (!(first == replay)) {
        std::cerr << "deterministic replay mismatch\n";
        return 3;
    }
    WriteTrace(first);
    return 0;
}
