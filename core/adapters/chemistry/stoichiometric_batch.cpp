#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kExtent = 3;
constexpr uint64_t kInitialA = 6000;
constexpr uint64_t kInitialB = 4000;
constexpr uint32_t kATagRow = 2;
constexpr uint32_t kATagColumn = 0;
constexpr uint32_t kBTagRow = 2;
constexpr uint32_t kBTagColumn = 1;

struct BatchResult {
    uint64_t retained_a = 0;
    uint64_t retained_b = 0;
    uint64_t accepted_a = 0;
    uint64_t accepted_b = 0;
    uint64_t exhaust_a = 0;
    uint64_t exhaust_b = 0;
    uint64_t product_pairs = 0;
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const BatchResult&) const = default;
};

struct RunResult {
    BatchResult safe{};
    BatchResult degraded{};
    BatchResult safety{};

    bool operator==(const RunResult&) const = default;
};

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(kExtent, 1, x, y, 0);
}

bool ConfigureSource(
    TensorlessFpmSandbox* sandbox,
    uint64_t index,
    uint64_t initial,
    uint64_t submitted,
    uint32_t route_axis,
    uint32_t tag_row,
    uint32_t tag_column) {
    TensorlessFpmSandboxNode node{};
    node.energy_subunits = initial;
    node.momentum[route_axis][route_axis] =
        static_cast<int64_t>(submitted);
    node.momentum[tag_row][tag_column] =
        static_cast<int64_t>(submitted);
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(
            sandbox, index, &node),
        "Tensorless_FpmSandboxSetNode(reagent)");
}

bool ReadNode(
    TensorlessFpmSandbox* sandbox,
    uint64_t index,
    TensorlessFpmSandboxNode& node) {
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxGetNode(
            sandbox, index, &node),
        "Tensorless_FpmSandboxGetNode(batch)");
}

bool ExecuteBatch(
    uint64_t product_headroom,
    uint64_t submitted_a,
    uint64_t submitted_b,
    BatchResult& result) {
    if (submitted_a > kInitialA ||
        submitted_b > kInitialB ||
        product_headroom >
            tensorless_adapter::kEnergyCeiling)
        return false;

    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(
            kExtent, kExtent, 1);
    const uint64_t source_a = Index(0, 1);
    const uint64_t source_b = Index(1, 0);
    const uint64_t product = Index(1, 1);
    if (!sandbox.Create(info) ||
        !ConfigureSource(
            sandbox.get(), source_a, kInitialA, submitted_a,
            0, kATagRow, kATagColumn) ||
        !ConfigureSource(
            sandbox.get(), source_b, kInitialB, submitted_b,
            1, kBTagRow, kBTagColumn))
        return false;

    TensorlessFpmSandboxNode product_node{};
    product_node.energy_subunits =
        tensorless_adapter::kEnergyCeiling -
        product_headroom;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), product, &product_node),
            "Tensorless_FpmSandboxSetNode(product)"))
        return false;

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[source_a] = submitted_a;
    payloads[source_b] = submitted_b;

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(batch)"))
        return false;

    TensorlessFpmSandboxNode a_node{};
    TensorlessFpmSandboxNode b_node{};
    if (!ReadNode(sandbox.get(), source_a, a_node) ||
        !ReadNode(sandbox.get(), source_b, b_node) ||
        !ReadNode(sandbox.get(), product, product_node))
        return false;

    const int64_t accepted_a =
        product_node.momentum[kATagRow][kATagColumn];
    const int64_t accepted_b =
        product_node.momentum[kBTagRow][kBTagColumn];
    const int64_t exhaust_a =
        stats.external_momentum_exhaust[
            kATagRow][kATagColumn];
    const int64_t exhaust_b =
        stats.external_momentum_exhaust[
            kBTagRow][kBTagColumn];
    if (accepted_a < 0 || accepted_b < 0 ||
        exhaust_a < 0 || exhaust_b < 0)
        return false;

    result = {};
    result.retained_a = a_node.energy_subunits;
    result.retained_b = b_node.energy_subunits;
    result.accepted_a = static_cast<uint64_t>(accepted_a);
    result.accepted_b = static_cast<uint64_t>(accepted_b);
    result.exhaust_a = static_cast<uint64_t>(exhaust_a);
    result.exhaust_b = static_cast<uint64_t>(exhaust_b);
    if (result.accepted_a != result.accepted_b)
        return false;
    result.product_pairs = result.accepted_a;
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];

    return result.external_exhaust ==
               result.exhaust_a + result.exhaust_b &&
           tensorless_adapter::ResidualsAreZero(stats);
}

bool Verify(
    std::string_view state,
    const BatchResult& actual,
    const BatchResult& expected) {
    if (actual == expected)
        return true;
    std::cerr << state << " batch differs from oracle\n";
    return false;
}

bool Execute(RunResult& result) {
    result = {};
    if (!ExecuteBatch(6000, 3000, 3000, result.safe) ||
        !ExecuteBatch(2000, 1000, 1000, result.degraded) ||
        !ExecuteBatch(2000, 3000, 3000, result.safety))
        return false;

    const BatchResult safe_oracle{
        3000, 1000, 3000, 3000, 0, 0, 3000, 0, 0, {}};
    const BatchResult degraded_oracle{
        5000, 3000, 1000, 1000, 0, 0, 1000, 0, 0, {}};
    const BatchResult safety_oracle{
        3000, 1000, 1000, 1000, 2000, 2000,
        1000, 4000, 0, {}};
    return Verify("safe", result.safe, safe_oracle) &&
           Verify("degraded", result.degraded, degraded_oracle) &&
           Verify("safety", result.safety, safety_oracle);
}

void WriteState(
    std::string_view state,
    const BatchResult& result,
    bool admission_failure) {
    std::cout
        << "{\"type\":\"batch_state\","
        << "\"state\":\"" << state << "\","
        << "\"retained_A\":" << result.retained_a << ","
        << "\"retained_B\":" << result.retained_b << ","
        << "\"accepted_A\":" << result.accepted_a << ","
        << "\"accepted_B\":" << result.accepted_b << ","
        << "\"exhaust_A\":" << result.exhaust_a << ","
        << "\"exhaust_B\":" << result.exhaust_b << ","
        << "\"product_AB_pairs\":"
        << result.product_pairs << ","
        << "\"admission_failure\":"
        << (admission_failure ? "true" : "false") << ","
        << "\"energy_residual\":"
        << result.energy_residual << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteState("safe_batch", result.safe, false);
    WriteState(
        "catalyst_degradation",
        result.degraded, false);
    WriteState(
        "failed_admission",
        result.safety, true);
    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"stoichiometric_batch\","
        << "\"reaction\":\"A+B->AB\","
        << "\"initial_A\":6000,"
        << "\"initial_B\":4000,"
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
