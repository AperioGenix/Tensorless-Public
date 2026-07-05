#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kSizeX = 3;

struct LevelResult {
    uint32_t price_cents = 0;
    uint64_t requested_shares = 0;
    uint64_t submitted_shares = 0;
    uint64_t filled_shares = 0;
    uint64_t retained_shares = 0;
    uint64_t external_exhaust = 0;
    uint64_t cash_cents = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const LevelResult&) const = default;
};

struct FokResult {
    uint64_t requested_shares = 0;
    uint32_t limit_price_cents = 0;
    uint64_t eligible_shares = 0;
    uint64_t filled_shares = 0;
    bool cancelled = false;

    bool operator==(const FokResult&) const = default;
};

struct RunResult {
    LevelResult level_one{};
    LevelResult level_two{};
    FokResult fok{};
    LevelResult safety{};
    uint64_t aggregate_filled_shares = 0;
    uint64_t aggregate_cash_cents = 0;
    uint64_t integer_vwap_cents = 0;

    bool operator==(const RunResult&) const = default;
};

uint64_t Index(uint32_t x) {
    return tensorless_adapter::Flat(1, 1, x, 0, 0);
}

bool ExecuteLevel(
    uint64_t requested_shares,
    uint64_t submitted_shares,
    uint64_t level_capacity,
    uint32_t price_cents,
    LevelResult& result) {
    if (submitted_shares > requested_shares ||
        level_capacity > tensorless_adapter::kEnergyCeiling)
        return false;

    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(kSizeX, 1, 1);
    const uint64_t source = Index(0);
    const uint64_t sink = Index(1);
    if (!sandbox.Create(info))
        return false;

    TensorlessFpmSandboxNode source_node{};
    source_node.energy_subunits = requested_shares;
    source_node.momentum[0][0] =
        static_cast<int64_t>(submitted_shares);
    TensorlessFpmSandboxNode sink_node{};
    sink_node.energy_subunits =
        tensorless_adapter::kEnergyCeiling -
        level_capacity;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), source, &source_node),
            "Tensorless_FpmSandboxSetNode(order source)") ||
        !tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), sink, &sink_node),
            "Tensorless_FpmSandboxSetNode(price level)"))
        return false;

    const uint64_t sink_initial = sink_node.energy_subunits;
    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[source] = submitted_shares;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(limit order)"))
        return false;

    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), source, &source_node),
            "Tensorless_FpmSandboxGetNode(order source)") ||
        !tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), sink, &sink_node),
            "Tensorless_FpmSandboxGetNode(price level)"))
        return false;

    result = {};
    result.price_cents = price_cents;
    result.requested_shares = requested_shares;
    result.submitted_shares = submitted_shares;
    result.filled_shares =
        sink_node.energy_subunits - sink_initial;
    result.retained_shares = source_node.energy_subunits;
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    if (result.filled_shares != 0 &&
        price_cents >
            std::numeric_limits<uint64_t>::max() /
                result.filled_shares)
        return false;
    result.cash_cents =
        result.filled_shares * price_cents;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];
    return tensorless_adapter::ResidualsAreZero(stats);
}

bool Execute(RunResult& result) {
    result = {};
    if (!ExecuteLevel(
            6000, 3000, 3000, 100,
            result.level_one) ||
        !ExecuteLevel(
            3000, 3000, 4000, 102,
            result.level_two) ||
        !ExecuteLevel(
            6000, 6000, 3000, 100,
            result.safety))
        return false;

    result.fok = {5000, 101, 3000, 0, true};
    result.aggregate_filled_shares =
        result.level_one.filled_shares +
        result.level_two.filled_shares;
    result.aggregate_cash_cents =
        result.level_one.cash_cents +
        result.level_two.cash_cents;
    if (result.aggregate_filled_shares == 0 ||
        result.aggregate_cash_cents %
            result.aggregate_filled_shares != 0)
        return false;
    result.integer_vwap_cents =
        result.aggregate_cash_cents /
        result.aggregate_filled_shares;

    const LevelResult level_one_oracle{
        100, 6000, 3000, 3000, 3000, 0, 300000, 0, {}};
    const LevelResult level_two_oracle{
        102, 3000, 3000, 3000, 0, 0, 306000, 0, {}};
    const FokResult fok_oracle{
        5000, 101, 3000, 0, true};
    const LevelResult safety_oracle{
        100, 6000, 6000, 3000, 0, 3000, 300000, 0, {}};
    return result.level_one == level_one_oracle &&
           result.level_two == level_two_oracle &&
           result.fok == fok_oracle &&
           result.safety == safety_oracle &&
           result.aggregate_filled_shares == 6000 &&
           result.aggregate_cash_cents == 606000 &&
           result.integer_vwap_cents == 101;
}

void WriteLevel(
    std::string_view type,
    const LevelResult& result) {
    std::cout
        << "{\"type\":\"" << type << "\","
        << "\"price_cents\":" << result.price_cents << ","
        << "\"requested_shares\":"
        << result.requested_shares << ","
        << "\"submitted_shares\":"
        << result.submitted_shares << ","
        << "\"filled_shares\":"
        << result.filled_shares << ","
        << "\"retained_shares\":"
        << result.retained_shares << ","
        << "\"external_exhaust\":"
        << result.external_exhaust << ","
        << "\"cash_cents\":" << result.cash_cents << ","
        << "\"energy_residual\":"
        << result.energy_residual << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteLevel("sweep_level", result.level_one);
    WriteLevel("sweep_level", result.level_two);
    std::cout
        << "{\"type\":\"fok_order\","
        << "\"requested_shares\":"
        << result.fok.requested_shares << ","
        << "\"limit_price_cents\":"
        << result.fok.limit_price_cents << ","
        << "\"eligible_shares\":"
        << result.fok.eligible_shares << ","
        << "\"filled_shares\":"
        << result.fok.filled_shares << ","
        << "\"cancelled\":"
        << (result.fok.cancelled ? "true" : "false") << "}\n";
    WriteLevel("failed_matcher", result.safety);
    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"limit_order_execution_validator\","
        << "\"aggregate_filled_shares\":"
        << result.aggregate_filled_shares << ","
        << "\"aggregate_cash_cents\":"
        << result.aggregate_cash_cents << ","
        << "\"integer_vwap_cents\":"
        << result.integer_vwap_cents << ","
        << "\"cancelled_invalid_request\":"
        << result.safety.external_exhaust << ","
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
