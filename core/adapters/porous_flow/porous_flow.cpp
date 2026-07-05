#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kSizeX = 3;
constexpr uint32_t kSizeY = 2;
constexpr uint64_t kSandstoneDemand = 8000;
constexpr uint64_t kFractureDemand = 2000;
constexpr uint64_t kSandstoneCapacity = 8000;

struct Position {
    uint32_t x;
    uint32_t y;
};

constexpr Position kSandstoneSource{0, 0};
constexpr Position kSandstoneSink{1, 0};
constexpr Position kFractureSource{0, 1};
constexpr Position kFractureSink{1, 1};

struct FlowResult {
    uint64_t sandstone_delivered = 0;
    uint64_t fracture_delivered = 0;
    uint64_t retained_manifold_pressure = 0;
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const FlowResult&) const = default;
};

struct RunResult {
    FlowResult optimal{};
    FlowResult degraded{};
    FlowResult safety{};

    bool operator==(const RunResult&) const = default;
};

uint64_t Index(Position position) {
    return tensorless_adapter::Flat(
        kSizeY, 1, position.x, position.y, 0);
}

bool ConfigureSource(
    TensorlessFpmSandbox* sandbox,
    Position position,
    uint64_t initial_energy,
    uint64_t submitted) {
    TensorlessFpmSandboxNode node{};
    node.energy_subunits = initial_energy;
    node.momentum[0][0] =
        static_cast<int64_t>(submitted);
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(
            sandbox, Index(position), &node),
        "Tensorless_FpmSandboxSetNode(flow source)");
}

bool ConfigureSink(
    TensorlessFpmSandbox* sandbox,
    Position position,
    uint64_t capacity) {
    if (capacity > tensorless_adapter::kEnergyCeiling)
        return false;
    TensorlessFpmSandboxNode node{};
    node.energy_subunits =
        tensorless_adapter::kEnergyCeiling - capacity;
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(
            sandbox, Index(position), &node),
        "Tensorless_FpmSandboxSetNode(flow sink)");
}

bool ReadEnergy(
    TensorlessFpmSandbox* sandbox,
    Position position,
    uint64_t& energy) {
    TensorlessFpmSandboxNode node{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox, Index(position), &node),
            "Tensorless_FpmSandboxGetNode(flow)"))
        return false;
    energy = node.energy_subunits;
    return true;
}

bool ExecuteTransaction(
    uint64_t fracture_capacity,
    uint64_t sandstone_submitted,
    uint64_t fracture_submitted,
    FlowResult& result) {
    if (sandstone_submitted > kSandstoneDemand ||
        fracture_submitted > kFractureDemand)
        return false;

    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(
            kSizeX, kSizeY, 1);
    if (!sandbox.Create(info) ||
        !ConfigureSource(
            sandbox.get(), kSandstoneSource,
            kSandstoneDemand, sandstone_submitted) ||
        !ConfigureSource(
            sandbox.get(), kFractureSource,
            kFractureDemand, fracture_submitted) ||
        !ConfigureSink(
            sandbox.get(), kSandstoneSink,
            kSandstoneCapacity) ||
        !ConfigureSink(
            sandbox.get(), kFractureSink,
            fracture_capacity))
        return false;

    const uint64_t sandstone_initial =
        tensorless_adapter::kEnergyCeiling -
        kSandstoneCapacity;
    const uint64_t fracture_initial =
        tensorless_adapter::kEnergyCeiling -
        fracture_capacity;
    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[Index(kSandstoneSource)] = sandstone_submitted;
    payloads[Index(kFractureSource)] = fracture_submitted;

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(porous flow)"))
        return false;

    uint64_t sandstone_sink_energy = 0;
    uint64_t fracture_sink_energy = 0;
    uint64_t sandstone_source_energy = 0;
    uint64_t fracture_source_energy = 0;
    if (!ReadEnergy(
            sandbox.get(), kSandstoneSink,
            sandstone_sink_energy) ||
        !ReadEnergy(
            sandbox.get(), kFractureSink,
            fracture_sink_energy) ||
        !ReadEnergy(
            sandbox.get(), kSandstoneSource,
            sandstone_source_energy) ||
        !ReadEnergy(
            sandbox.get(), kFractureSource,
            fracture_source_energy))
        return false;

    result = {};
    result.sandstone_delivered =
        sandstone_sink_energy - sandstone_initial;
    result.fracture_delivered =
        fracture_sink_energy - fracture_initial;
    result.retained_manifold_pressure =
        sandstone_source_energy + fracture_source_energy;
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];
    return tensorless_adapter::ResidualsAreZero(stats);
}

bool Verify(
    std::string_view state,
    const FlowResult& actual,
    const FlowResult& expected) {
    if (actual == expected)
        return true;
    std::cerr << state << " flow differs from oracle\n";
    return false;
}

bool Execute(RunResult& result) {
    result = {};
    if (!ExecuteTransaction(2000, 8000, 2000, result.optimal) ||
        !ExecuteTransaction(500, 8000, 500, result.degraded) ||
        !ExecuteTransaction(500, 8000, 2000, result.safety))
        return false;

    const FlowResult optimal_oracle{
        8000, 2000, 0, 0, 0, {}};
    const FlowResult degraded_oracle{
        8000, 500, 1500, 0, 0, {}};
    const FlowResult safety_oracle{
        8000, 500, 0, 1500, 0, {}};
    return Verify("optimal", result.optimal, optimal_oracle) &&
           Verify("degraded", result.degraded, degraded_oracle) &&
           Verify("safety", result.safety, safety_oracle);
}

void WriteState(
    std::string_view state,
    const FlowResult& result,
    bool pressure_event,
    bool release_event) {
    std::cout
        << "{\"type\":\"flow_state\","
        << "\"state\":\"" << state << "\","
        << "\"sandstone_delivered\":"
        << result.sandstone_delivered << ","
        << "\"fracture_delivered\":"
        << result.fracture_delivered << ","
        << "\"retained_manifold_pressure\":"
        << result.retained_manifold_pressure << ","
        << "\"external_exhaust\":"
        << result.external_exhaust << ","
        << "\"wellhead_pressure_buildup\":"
        << (pressure_event ? "true" : "false") << ","
        << "\"overpressure_boundary_release\":"
        << (release_event ? "true" : "false") << ","
        << "\"energy_residual\":"
        << result.energy_residual << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteState("optimal_flow", result.optimal, false, false);
    WriteState(
        "permeability_degradation",
        result.degraded, true, false);
    WriteState(
        "controller_failure",
        result.safety, false, true);
    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"porous_flow_capacity\","
        << "\"injection_demand\":10000,"
        << "\"degraded_retained_pressure\":1500,"
        << "\"safety_case_external_exhaust\":1500,"
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
