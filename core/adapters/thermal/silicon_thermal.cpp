#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kSizeX = 3;
constexpr uint32_t kSizeY = 3;
constexpr uint32_t kSizeZ = 2;
constexpr uint64_t kGeneratedHeat = 10000;
constexpr uint64_t kLateralCapacity = 1000;

struct RouteResult {
    uint64_t tim = 0;
    std::array<uint64_t, 4> lateral{};
    uint64_t retained_source_energy = 0;
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const RouteResult&) const = default;
};

struct RunResult {
    RouteResult safe{};
    RouteResult degraded{};
    RouteResult safety{};

    bool operator==(const RunResult&) const = default;
};

struct Position {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

constexpr Position kSource{1, 1, 0};
constexpr Position kTim{1, 1, 1};
constexpr std::array<Position, 4> kLateral{{
    {0, 1, 0},
    {2, 1, 0},
    {1, 0, 0},
    {1, 2, 0},
}};

uint64_t Index(Position position) {
    return tensorless_adapter::Flat(
        kSizeY, kSizeZ,
        position.x, position.y, position.z);
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
        "Tensorless_FpmSandboxSetNode(thermal sink)");
}

bool ExecuteTransaction(
    uint64_t tim_capacity,
    uint64_t submitted_heat,
    uint64_t positive_z_weight,
    RouteResult& result) {
    if (submitted_heat > kGeneratedHeat ||
        positive_z_weight >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()))
        return false;

    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(
            kSizeX, kSizeY, kSizeZ);
    info.isotropic_face_weight = kLateralCapacity;

    tensorless_adapter::Sandbox sandbox;
    if (!sandbox.Create(info))
        return false;

    TensorlessFpmSandboxNode source{};
    source.energy_subunits = kGeneratedHeat;
    source.momentum[2][2] =
        static_cast<int64_t>(positive_z_weight);
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), Index(kSource), &source),
            "Tensorless_FpmSandboxSetNode(thermal source)") ||
        !ConfigureSink(sandbox.get(), kTim, tim_capacity))
        return false;
    for (Position position : kLateral)
        if (!ConfigureSink(
                sandbox.get(), position, kLateralCapacity))
            return false;

    const uint64_t tim_initial =
        tensorless_adapter::kEnergyCeiling - tim_capacity;
    const uint64_t lateral_initial =
        tensorless_adapter::kEnergyCeiling -
        kLateralCapacity;
    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[Index(kSource)] = submitted_heat;

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(thermal)"))
        return false;

    TensorlessFpmSandboxNode node{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), Index(kSource), &node),
            "Tensorless_FpmSandboxGetNode(thermal source)"))
        return false;

    result = {};
    result.retained_source_energy = node.energy_subunits;
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];

    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), Index(kTim), &node),
            "Tensorless_FpmSandboxGetNode(TIM)"))
        return false;
    result.tim = node.energy_subunits - tim_initial;

    for (size_t index = 0; index < kLateral.size(); ++index) {
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxGetNode(
                    sandbox.get(), Index(kLateral[index]), &node),
                "Tensorless_FpmSandboxGetNode(lateral)"))
            return false;
        result.lateral[index] =
            node.energy_subunits - lateral_initial;
    }
    return tensorless_adapter::ResidualsAreZero(stats);
}

bool Verify(
    std::string_view name,
    const RouteResult& actual,
    const RouteResult& expected) {
    if (actual == expected)
        return true;
    std::cerr << name << " transaction differs from oracle\n";
    return false;
}

bool Execute(RunResult& result) {
    result = {};
    if (!ExecuteTransaction(8000, 10000, 5000, result.safe) ||
        !ExecuteTransaction(2000, 6000, 1000, result.degraded) ||
        !ExecuteTransaction(2000, 10000, 5000, result.safety))
        return false;

    const RouteResult safe_oracle{
        6000, {1000, 1000, 1000, 1000}, 0, 0, 0, {}};
    const RouteResult degraded_oracle{
        2000, {1000, 1000, 1000, 1000}, 4000, 0, 0, {}};
    const RouteResult safety_oracle{
        2000, {1000, 1000, 1000, 1000}, 0, 4000, 0, {}};
    return Verify("safe", result.safe, safe_oracle) &&
           Verify("degraded", result.degraded, degraded_oracle) &&
           Verify("safety", result.safety, safety_oracle);
}

void WriteTransaction(
    std::string_view state,
    const RouteResult& result,
    bool throttle_event) {
    std::cout
        << "{\"type\":\"thermal_state\","
        << "\"state\":\"" << state << "\","
        << "\"tim_delivered\":" << result.tim << ","
        << "\"lateral_delivered\":["
        << result.lateral[0] << ","
        << result.lateral[1] << ","
        << result.lateral[2] << ","
        << result.lateral[3] << "],"
        << "\"retained_source_energy\":"
        << result.retained_source_energy << ","
        << "\"external_exhaust\":"
        << result.external_exhaust << ","
        << "\"thermal_throttle_event\":"
        << (throttle_event ? "true" : "false") << ","
        << "\"energy_residual\":"
        << result.energy_residual << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteTransaction("optimal_cooling", result.safe, false);
    WriteTransaction("tim_degradation", result.degraded, true);
    WriteTransaction("controller_failure", result.safety, false);
    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"silicon_thermal\","
        << "\"generated_heat\":10000,"
        << "\"degraded_retained_heat\":4000,"
        << "\"safety_case_external_exhaust\":4000,"
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
