#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
constexpr std::string_view kSequence = "HHPHHH";
constexpr uint32_t kExtent = 3;

struct Point {
    int32_t x;
    int32_t y;

    bool operator==(const Point&) const = default;
};

struct CandidateResult {
    bool chain_connected = false;
    bool self_avoiding = false;
    uint32_t hydrophobic_contacts = 0;
    int32_t energy = 0;

    bool operator==(const CandidateResult&) const = default;
};

struct PackingResult {
    std::array<uint64_t, 3> accepted{};
    std::array<uint64_t, 3> exhaust{};
    uint64_t total_accepted = 0;
    uint64_t total_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const PackingResult&) const = default;
};

struct RunResult {
    CandidateResult straight{};
    CandidateResult folded{};
    CandidateResult clashing{};
    PackingResult packing{};

    bool operator==(const RunResult&) const = default;
};

constexpr std::array<Point, 6> kStraight{{
    {0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0},
}};
constexpr std::array<Point, 6> kFolded{{
    {0, 0}, {1, 0}, {2, 0}, {2, 1}, {1, 1}, {0, 1},
}};
constexpr std::array<Point, 6> kClashing{{
    {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {0, -1},
}};

uint32_t Manhattan(Point left, Point right) {
    return static_cast<uint32_t>(
        std::abs(left.x - right.x) +
        std::abs(left.y - right.y));
}

template <size_t Size>
CandidateResult EvaluateCandidate(
    std::string_view sequence,
    const std::array<Point, Size>& coordinates) {
    CandidateResult result{};
    if (sequence.size() != Size)
        return result;

    result.chain_connected = true;
    for (size_t index = 1; index < Size; ++index)
        if (Manhattan(
                coordinates[index - 1],
                coordinates[index]) != 1)
            result.chain_connected = false;

    result.self_avoiding = true;
    for (size_t left = 0; left < Size; ++left)
        for (size_t right = left + 1; right < Size; ++right)
            if (coordinates[left] == coordinates[right])
                result.self_avoiding = false;

    if (!result.chain_connected || !result.self_avoiding)
        return result;

    for (size_t left = 0; left < Size; ++left) {
        if (sequence[left] != 'H')
            continue;
        for (size_t right = left + 2; right < Size; ++right) {
            if (sequence[right] == 'H' &&
                Manhattan(coordinates[left], coordinates[right]) == 1)
                ++result.hydrophobic_contacts;
        }
    }
    result.energy =
        -static_cast<int32_t>(result.hydrophobic_contacts);
    return result;
}

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(kExtent, 1, x, y, 0);
}

bool ConfigureRegion(
    TensorlessFpmSandbox* sandbox,
    uint64_t index,
    uint64_t demand,
    uint32_t route_axis,
    int64_t route_sign,
    uint32_t tag_row,
    uint32_t tag_column) {
    TensorlessFpmSandboxNode node{};
    node.energy_subunits = demand;
    node.momentum[route_axis][route_axis] =
        route_sign * static_cast<int64_t>(demand);
    node.momentum[tag_row][tag_column] =
        static_cast<int64_t>(demand);
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(
            sandbox, index, &node),
        "Tensorless_FpmSandboxSetNode(packing region)");
}

bool ExecutePacking(PackingResult& result) {
    constexpr std::array<uint64_t, 3> demands{4000, 3000, 2000};
    constexpr std::array<uint32_t, 3> tag_rows{2, 2, 0};
    constexpr std::array<uint32_t, 3> tag_columns{0, 1, 2};

    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(
            kExtent, kExtent, 1);
    const std::array<uint64_t, 3> sources{
        Index(0, 1), Index(2, 1), Index(1, 0)};
    const uint64_t sink = Index(1, 1);
    if (!sandbox.Create(info) ||
        !ConfigureRegion(
            sandbox.get(), sources[0], demands[0],
            0, 1, tag_rows[0], tag_columns[0]) ||
        !ConfigureRegion(
            sandbox.get(), sources[1], demands[1],
            0, -1, tag_rows[1], tag_columns[1]) ||
        !ConfigureRegion(
            sandbox.get(), sources[2], demands[2],
            1, 1, tag_rows[2], tag_columns[2]))
        return false;

    TensorlessFpmSandboxNode sink_node{};
    sink_node.energy_subunits =
        tensorless_adapter::kEnergyCeiling - 6000;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), sink, &sink_node),
            "Tensorless_FpmSandboxSetNode(packing sink)"))
        return false;

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    for (size_t index = 0; index < sources.size(); ++index)
        payloads[sources[index]] = demands[index];

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(packing)"))
        return false;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), sink, &sink_node),
            "Tensorless_FpmSandboxGetNode(packing sink)"))
        return false;

    result = {};
    for (size_t index = 0; index < demands.size(); ++index) {
        const int64_t accepted =
            sink_node.momentum[
                tag_rows[index]][tag_columns[index]];
        const int64_t exhaust =
            stats.external_momentum_exhaust[
                tag_rows[index]][tag_columns[index]];
        if (accepted < 0 || exhaust < 0)
            return false;
        result.accepted[index] =
            static_cast<uint64_t>(accepted);
        result.exhaust[index] =
            static_cast<uint64_t>(exhaust);
        result.total_accepted += result.accepted[index];
        result.total_exhaust += result.exhaust[index];
    }
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];

    return result.total_exhaust ==
               stats.external_energy_exhaust_subunits &&
           tensorless_adapter::ResidualsAreZero(stats);
}

bool Execute(RunResult& result) {
    result = {};
    result.straight =
        EvaluateCandidate(kSequence, kStraight);
    result.folded =
        EvaluateCandidate(kSequence, kFolded);
    result.clashing =
        EvaluateCandidate(kSequence, kClashing);
    if (!ExecutePacking(result.packing))
        return false;

    const CandidateResult straight_oracle{
        true, true, 0, 0};
    const CandidateResult folded_oracle{
        true, true, 2, -2};
    const CandidateResult clashing_oracle{
        true, false, 0, 0};
    const PackingResult packing_oracle{
        {2667, 2000, 1333},
        {1333, 1000, 667},
        6000, 3000, 0, {}};
    return result.straight == straight_oracle &&
           result.folded == folded_oracle &&
           result.clashing == clashing_oracle &&
           result.packing == packing_oracle;
}

void WriteCandidate(
    std::string_view name,
    const CandidateResult& result) {
    std::cout
        << "{\"type\":\"candidate\","
        << "\"name\":\"" << name << "\","
        << "\"chain_connected\":"
        << (result.chain_connected ? "true" : "false") << ","
        << "\"self_avoiding\":"
        << (result.self_avoiding ? "true" : "false") << ","
        << "\"hydrophobic_contacts\":"
        << result.hydrophobic_contacts << ","
        << "\"energy\":" << result.energy << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteCandidate("straight", result.straight);
    WriteCandidate("folded_u", result.folded);
    WriteCandidate("clashing", result.clashing);
    std::cout
        << "{\"type\":\"steric_contention\","
        << "\"demands\":[4000,3000,2000],"
        << "\"capacity\":6000,"
        << "\"accepted\":["
        << result.packing.accepted[0] << ","
        << result.packing.accepted[1] << ","
        << result.packing.accepted[2] << "],"
        << "\"exhaust\":["
        << result.packing.exhaust[0] << ","
        << result.packing.exhaust[1] << ","
        << result.packing.exhaust[2] << "],"
        << "\"steric_clash_event\":true,"
        << "\"energy_residual\":"
        << result.packing.energy_residual << "}\n"
        << "{\"type\":\"summary\","
        << "\"adapter\":\"protein_lattice_candidate\","
        << "\"sequence\":\"HHPHHH\","
        << "\"preferred_candidate\":\"folded_u\","
        << "\"preferred_energy\":-2,"
        << "\"packing_exhaust\":3000,"
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
