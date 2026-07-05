#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr uint32_t kExtent = 3;
constexpr uint64_t kQuantum = 1000;

struct RowResult {
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t carry_in = 0;
    uint32_t input_count = 0;
    uint64_t sum_delivered = 0;
    uint64_t carry_delivered = 0;
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const RowResult&) const = default;
};

struct RunResult {
    std::array<RowResult, 8> rows{};
    bool invalid_gate_rejected = false;

    bool operator==(const RunResult&) const = default;
};

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(kExtent, 1, x, y, 0);
}

TensorlessFpmSandboxCreateInfo RoutingConfig() {
    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(
            kExtent, kExtent, 1);
    info.isotropic_face_weight = 0;
    return info;
}

bool ConfigureNode(
    TensorlessFpmSandbox* sandbox,
    uint64_t index,
    uint64_t energy,
    uint64_t sum_weight,
    uint64_t carry_weight) {
    TensorlessFpmSandboxNode node{};
    node.energy_subunits = energy;
    node.momentum[0][0] =
        static_cast<int64_t>(sum_weight);
    node.momentum[1][1] =
        static_cast<int64_t>(carry_weight);
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(
            sandbox, index, &node),
        "Tensorless_FpmSandboxSetNode(full adder)");
}

bool ExecuteRow(
    uint32_t a,
    uint32_t b,
    uint32_t carry_in,
    RowResult& result) {
    if (a > 1 || b > 1 || carry_in > 1)
        return false;
    const uint32_t count = a + b + carry_in;
    const uint64_t sum_payload =
        static_cast<uint64_t>(count % 2) * kQuantum;
    const uint64_t carry_payload =
        static_cast<uint64_t>(count / 2) * 2 * kQuantum;
    const uint64_t input_energy =
        static_cast<uint64_t>(count) * kQuantum;
    if (sum_payload + carry_payload != input_energy)
        return false;

    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info = RoutingConfig();
    const uint64_t source = Index(1, 1);
    const uint64_t sum_sink = Index(2, 1);
    const uint64_t carry_sink = Index(1, 2);
    if (!sandbox.Create(info) ||
        !ConfigureNode(
            sandbox.get(), source, input_energy,
            sum_payload, carry_payload) ||
        !ConfigureNode(
            sandbox.get(), sum_sink,
            tensorless_adapter::kEnergyCeiling - kQuantum,
            0, 0) ||
        !ConfigureNode(
            sandbox.get(), carry_sink,
            tensorless_adapter::kEnergyCeiling - 2 * kQuantum,
            0, 0))
        return false;

    const uint64_t node_count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(
        static_cast<size_t>(node_count), 0);
    std::vector<uint64_t> payloads(
        static_cast<size_t>(node_count), 0);
    payloads[source] = input_energy;

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                node_count, &stats),
            "Tensorless_FpmSandboxStep(full adder)"))
        return false;

    TensorlessFpmSandboxNode sum_node{};
    TensorlessFpmSandboxNode carry_node{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), sum_sink, &sum_node),
            "Tensorless_FpmSandboxGetNode(sum)") ||
        !tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), carry_sink, &carry_node),
            "Tensorless_FpmSandboxGetNode(carry)"))
        return false;

    result = {};
    result.a = a;
    result.b = b;
    result.carry_in = carry_in;
    result.input_count = count;
    result.sum_delivered =
        sum_node.energy_subunits -
        (tensorless_adapter::kEnergyCeiling - kQuantum);
    result.carry_delivered =
        carry_node.energy_subunits -
        (tensorless_adapter::kEnergyCeiling - 2 * kQuantum);
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];

    return result.sum_delivered == sum_payload &&
           result.carry_delivered == carry_payload &&
           result.external_exhaust == 0 &&
           tensorless_adapter::ResidualsAreZero(stats);
}

bool InvalidGateRejected() {
    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info = RoutingConfig();
    const uint64_t source = Index(1, 1);
    if (!sandbox.Create(info) ||
        !ConfigureNode(
            sandbox.get(), source, 2 * kQuantum,
            kQuantum, 2 * kQuantum))
        return false;

    const uint64_t node_count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(
        static_cast<size_t>(node_count), 0);
    std::vector<uint64_t> payloads(
        static_cast<size_t>(node_count), 0);
    payloads[source] = 3 * kQuantum;
    TensorlessFpmSandboxStats stats{};
    return Tensorless_FpmSandboxStep(
               sandbox.get(), actions.data(), payloads.data(),
               node_count, &stats) ==
           TENSORLESS_STATUS_STATE_ERROR;
}

bool Execute(RunResult& result) {
    result = {};
    size_t row = 0;
    for (uint32_t a = 0; a <= 1; ++a)
        for (uint32_t b = 0; b <= 1; ++b)
            for (uint32_t carry_in = 0;
                 carry_in <= 1; ++carry_in) {
                if (!ExecuteRow(
                        a, b, carry_in, result.rows[row]))
                    return false;
                const uint32_t count = a + b + carry_in;
                const RowResult oracle{
                    a, b, carry_in, count,
                    static_cast<uint64_t>(count % 2) * kQuantum,
                    static_cast<uint64_t>(count / 2) * 2 * kQuantum,
                    0, 0, {}};
                if (!(result.rows[row] == oracle))
                    return false;
                ++row;
            }
    result.invalid_gate_rejected = InvalidGateRejected();
    return result.invalid_gate_rejected;
}

void WriteTrace(const RunResult& result) {
    for (const RowResult& row : result.rows) {
        std::cout
            << "{\"type\":\"truth_row\","
            << "\"A\":" << row.a << ","
            << "\"B\":" << row.b << ","
            << "\"carry_in\":" << row.carry_in << ","
            << "\"input_count\":" << row.input_count << ","
            << "\"sum_bit\":"
            << row.sum_delivered / kQuantum << ","
            << "\"carry_bit\":"
            << row.carry_delivered / (2 * kQuantum) << ","
            << "\"sum_payload\":"
            << row.sum_delivered << ","
            << "\"carry_payload\":"
            << row.carry_delivered << ","
            << "\"energy_residual\":"
            << row.energy_residual << "}\n";
    }
    std::cout
        << "{\"type\":\"safety_case\","
        << "\"input_energy\":2000,"
        << "\"requested_output\":3000,"
        << "\"invalid_gate_rejected\":"
        << (result.invalid_gate_rejected ? "true" : "false")
        << "}\n"
        << "{\"type\":\"summary\","
        << "\"adapter\":\"full_adder_signal_validator\","
        << "\"truth_rows\":8,"
        << "\"quantum\":1000,"
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
