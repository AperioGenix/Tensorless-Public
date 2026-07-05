#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kSize = 4;
constexpr std::array<uint8_t, kSize> kSouthSeed{1, 0, 1, 1};
constexpr std::array<uint8_t, kSize> kWestSeed{0, 1, 1, 0};
constexpr std::array<uint64_t, 4> kExactInventory{0, 6, 5, 5};
constexpr std::array<std::string_view, kSize> kExpectedRows{
    "1101",
    "0110",
    "1011",
    "1101",
};

struct Site {
    int8_t value = -1;
    int8_t tile_type = -1;

    bool operator==(const Site&) const = default;
};

struct CoreAdmission {
    uint64_t accepted = 0;
    uint64_t invalid_proposal_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const CoreAdmission&) const = default;
};

struct AssemblyResult {
    std::array<std::array<Site, kSize>, kSize> lattice{};
    std::array<uint64_t, 4> inventory_initial{};
    std::array<uint64_t, 4> inventory_remaining{};
    uint64_t initial_tokens = 0;
    uint64_t tokens_consumed = 0;
    uint64_t tokens_remaining = 0;
    uint64_t sites_filled = 0;
    uint64_t core_accepted = 0;
    uint64_t core_exhaust = 0;
    uint64_t frontier_transactions = 0;

    bool operator==(const AssemblyResult&) const = default;
};

struct SafetyResult {
    uint64_t proposals = 0;
    uint64_t accepted = 0;
    uint64_t invalid_proposal_exhaust = 0;
    uint64_t token_reservations = 0;
    uint64_t tokens_consumed = 0;
    uint64_t reservations_released = 0;
    uint64_t tokens_remaining = 0;
    uint64_t physical_mass_exhaust = 0;
    bool informational_energy_measured = false;
    int64_t energy_residual = 0;

    bool operator==(const SafetyResult&) const = default;
};

struct RunResult {
    AssemblyResult complete{};
    AssemblyResult starved{};
    AssemblyResult token_limited{};
    SafetyResult duplicate{};
    SafetyResult overwrite{};

    bool operator==(const RunResult&) const = default;
};

uint64_t LaneIndex(uint32_t lanes, uint32_t x, uint32_t lane) {
    return tensorless_adapter::Flat(lanes, 1, x, lane, 0);
}

bool ReadNode(
    TensorlessFpmSandbox* sandbox,
    uint64_t index,
    TensorlessFpmSandboxNode& node) {
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxGetNode(sandbox, index, &node),
        "Tensorless_FpmSandboxGetNode(tile assembly)");
}

bool AdmitDistinctPlacements(
    uint32_t placements,
    CoreAdmission& result) {
    if (placements == 0 || placements > kSize)
        return false;
    tensorless_adapter::Sandbox sandbox;
    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(2, placements, 1);
    if (!sandbox.Create(info))
        return false;

    const uint64_t node_count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(
        static_cast<size_t>(node_count), 0);
    std::vector<uint64_t> payloads(
        static_cast<size_t>(node_count), 0);
    for (uint32_t lane = 0; lane < placements; ++lane) {
        TensorlessFpmSandboxNode source{};
        source.energy_subunits = 1;
        source.momentum[0][0] = 1;
        TensorlessFpmSandboxNode target{};
        target.energy_subunits =
            tensorless_adapter::kEnergyCeiling - 1;
        const uint64_t source_index = LaneIndex(placements, 0, lane);
        const uint64_t target_index = LaneIndex(placements, 1, lane);
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(), source_index, &source),
                "Tensorless_FpmSandboxSetNode(tile source)") ||
            !tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(), target_index, &target),
                "Tensorless_FpmSandboxSetNode(tile site)"))
            return false;
        payloads[source_index] = 1;
    }

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                node_count, &stats),
            "Tensorless_FpmSandboxStep(tile frontier)"))
        return false;

    result = {};
    for (uint32_t lane = 0; lane < placements; ++lane) {
        TensorlessFpmSandboxNode target{};
        if (!ReadNode(
                sandbox.get(), LaneIndex(placements, 1, lane), target))
            return false;
        result.accepted +=
            target.energy_subunits -
            (tensorless_adapter::kEnergyCeiling - 1);
    }
    result.invalid_proposal_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];
    return tensorless_adapter::ResidualsAreZero(stats) &&
           result.accepted == placements &&
           result.invalid_proposal_exhaust == 0;
}

uint8_t TileType(uint8_t west, uint8_t south) {
    return static_cast<uint8_t>(west * 2U + south);
}

uint8_t TileOutput(uint8_t west, uint8_t south) {
    return static_cast<uint8_t>(west ^ south);
}

bool Assemble(
    const std::array<uint64_t, 4>& inventory,
    uint64_t activation_tokens,
    AssemblyResult& result) {
    result = {};
    result.inventory_initial = inventory;
    result.inventory_remaining = inventory;
    result.initial_tokens = activation_tokens;
    result.tokens_remaining = activation_tokens;

    for (uint32_t diagonal = 0;
         diagonal < 2U * kSize - 1U;
         ++diagonal) {
        struct Candidate {
            uint32_t x;
            uint32_t y;
            uint8_t type;
            uint8_t value;
        };
        std::vector<Candidate> candidates;
        std::array<uint64_t, 4> reserved_by_type{};
        for (uint32_t y = 0; y < kSize; ++y) {
            if (diagonal < y)
                continue;
            const uint32_t x = diagonal - y;
            if (x >= kSize || result.tokens_remaining <= candidates.size())
                continue;
            if (result.lattice[y][x].value >= 0)
                return false;
            if (y != 0 && result.lattice[y - 1][x].value < 0)
                continue;
            if (x != 0 && result.lattice[y][x - 1].value < 0)
                continue;
            const uint8_t south =
                y == 0
                    ? kSouthSeed[x]
                    : static_cast<uint8_t>(
                          result.lattice[y - 1][x].value);
            const uint8_t west =
                x == 0
                    ? kWestSeed[y]
                    : static_cast<uint8_t>(
                          result.lattice[y][x - 1].value);
            const uint8_t type = TileType(west, south);
            if (reserved_by_type[type] >=
                result.inventory_remaining[type])
                continue;
            ++reserved_by_type[type];
            candidates.push_back({
                x, y, type, TileOutput(west, south)});
        }
        if (candidates.empty())
            continue;

        CoreAdmission admission{};
        if (!AdmitDistinctPlacements(
                static_cast<uint32_t>(candidates.size()), admission))
            return false;
        ++result.frontier_transactions;
        result.core_accepted += admission.accepted;
        result.core_exhaust += admission.invalid_proposal_exhaust;
        for (const Candidate& candidate : candidates) {
            if (result.lattice[candidate.y][candidate.x].value >= 0 ||
                result.inventory_remaining[candidate.type] == 0 ||
                result.tokens_remaining == 0)
                return false;
            result.lattice[candidate.y][candidate.x] = {
                static_cast<int8_t>(candidate.value),
                static_cast<int8_t>(candidate.type),
            };
            --result.inventory_remaining[candidate.type];
            --result.tokens_remaining;
            ++result.tokens_consumed;
            ++result.sites_filled;
        }
    }
    return result.tokens_consumed == result.sites_filled &&
           result.initial_tokens ==
               result.tokens_consumed + result.tokens_remaining &&
           result.core_accepted == result.sites_filled &&
           result.core_exhaust == 0;
}

bool ExecuteCapacitySafety(
    bool already_filled,
    SafetyResult& result) {
    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(3, 3, 1);
    if (!sandbox.Create(info))
        return false;
    const uint64_t sink = tensorless_adapter::Flat(3, 1, 1, 1, 0);
    const std::array<uint64_t, 2> sources{
        tensorless_adapter::Flat(3, 1, 0, 1, 0),
        tensorless_adapter::Flat(3, 1, 1, 0, 0),
    };
    const uint32_t proposals = already_filled ? 1 : 2;
    for (uint32_t proposal = 0; proposal < proposals; ++proposal) {
        TensorlessFpmSandboxNode source{};
        source.energy_subunits = 1;
        source.momentum[proposal][proposal] = 1;
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(), sources[proposal], &source),
                "Tensorless_FpmSandboxSetNode(safety proposal)"))
            return false;
    }
    TensorlessFpmSandboxNode site{};
    site.energy_subunits =
        already_filled
            ? tensorless_adapter::kEnergyCeiling
            : tensorless_adapter::kEnergyCeiling - 1;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), sink, &site),
            "Tensorless_FpmSandboxSetNode(safety site)"))
        return false;

    const uint64_t node_count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(
        static_cast<size_t>(node_count), 0);
    std::vector<uint64_t> payloads(
        static_cast<size_t>(node_count), 0);
    for (uint32_t proposal = 0; proposal < proposals; ++proposal)
        payloads[sources[proposal]] = 1;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                node_count, &stats),
            "Tensorless_FpmSandboxStep(site safety)") ||
        !ReadNode(sandbox.get(), sink, site))
        return false;

    const uint64_t initial_site =
        already_filled
            ? tensorless_adapter::kEnergyCeiling
            : tensorless_adapter::kEnergyCeiling - 1;
    result = {};
    result.proposals = proposals;
    result.accepted = site.energy_subunits - initial_site;
    result.invalid_proposal_exhaust =
        stats.external_energy_exhaust_subunits;
    result.token_reservations = proposals;
    result.tokens_consumed = result.accepted;
    result.reservations_released =
        proposals - result.accepted;
    result.tokens_remaining = result.reservations_released;
    result.physical_mass_exhaust = 0;
    result.informational_energy_measured = false;
    result.energy_residual = stats.energy_conservation_residual;
    return tensorless_adapter::ResidualsAreZero(stats) &&
           result.accepted + result.invalid_proposal_exhaust ==
               proposals &&
           result.tokens_consumed == result.accepted &&
           result.token_reservations ==
               result.tokens_consumed +
               result.reservations_released;
}

std::array<std::string, kSize> Rows(const AssemblyResult& result) {
    std::array<std::string, kSize> rows{};
    for (uint32_t y = 0; y < kSize; ++y)
        for (uint32_t x = 0; x < kSize; ++x)
            rows[y].push_back(
                result.lattice[y][x].value < 0
                    ? '.'
                    : static_cast<char>(
                          '0' + result.lattice[y][x].value));
    return rows;
}

bool Execute(RunResult& result) {
    result = {};
    std::array<uint64_t, 4> starved_inventory =
        kExactInventory;
    --starved_inventory[1];
    if (!Assemble(kExactInventory, 16, result.complete) ||
        !Assemble(starved_inventory, 16, result.starved) ||
        !Assemble(kExactInventory, 10, result.token_limited) ||
        !ExecuteCapacitySafety(false, result.duplicate) ||
        !ExecuteCapacitySafety(true, result.overwrite))
        return false;

    const std::array<std::string, kSize> complete_rows =
        Rows(result.complete);
    const std::array<std::string, kSize> starved_rows =
        Rows(result.starved);
    const std::array<std::string, kSize> limited_rows =
        Rows(result.token_limited);
    for (uint32_t row = 0; row < kSize; ++row)
        if (complete_rows[row] != kExpectedRows[row])
            return false;
    return result.complete.sites_filled == 16 &&
           result.complete.inventory_remaining ==
               std::array<uint64_t, 4>{0, 0, 0, 0} &&
           result.starved.sites_filled == 15 &&
           starved_rows ==
               std::array<std::string, kSize>{
                   "1101", "0110", "1011", "110."} &&
           result.token_limited.sites_filled == 10 &&
           limited_rows ==
               std::array<std::string, kSize>{
                   "1101", "011.", "10..", "1..."} &&
           result.duplicate ==
               SafetyResult{2, 1, 1, 2, 1, 1, 1, 0, false, 0} &&
           result.overwrite ==
               SafetyResult{1, 0, 1, 1, 0, 1, 1, 0, false, 0};
}

void WriteInventory(const std::array<uint64_t, 4>& inventory) {
    std::cout
        << "[" << inventory[0] << "," << inventory[1]
        << "," << inventory[2] << "," << inventory[3] << "]";
}

void WriteRows(const std::array<std::string, kSize>& rows) {
    std::cout << "[";
    for (uint32_t row = 0; row < kSize; ++row) {
        if (row != 0)
            std::cout << ",";
        std::cout << "\"" << rows[row] << "\"";
    }
    std::cout << "]";
}

void WriteAssembly(
    std::string_view name,
    const AssemblyResult& result) {
    std::cout
        << "{\"type\":\"assembly\","
        << "\"name\":\"" << name << "\","
        << "\"rows\":";
    WriteRows(Rows(result));
    std::cout
        << ",\"inventory_initial\":";
    WriteInventory(result.inventory_initial);
    std::cout
        << ",\"inventory_remaining\":";
    WriteInventory(result.inventory_remaining);
    std::cout
        << ",\"initial_tokens\":" << result.initial_tokens
        << ",\"tokens_consumed\":" << result.tokens_consumed
        << ",\"tokens_remaining\":" << result.tokens_remaining
        << ",\"sites_filled\":" << result.sites_filled
        << ",\"core_accepted\":" << result.core_accepted
        << ",\"invalid_proposal_exhaust\":" << result.core_exhaust
        << ",\"frontier_transactions\":"
        << result.frontier_transactions << "}\n";
}

void WriteSafety(
    std::string_view name,
    const SafetyResult& result) {
    std::cout
        << "{\"type\":\"safety_case\","
        << "\"name\":\"" << name << "\","
        << "\"proposals\":" << result.proposals
        << ",\"accepted\":" << result.accepted
        << ",\"invalid_proposal_exhaust\":"
        << result.invalid_proposal_exhaust
        << ",\"token_reservations\":" << result.token_reservations
        << ",\"tokens_consumed\":" << result.tokens_consumed
        << ",\"reservations_released\":"
        << result.reservations_released
        << ",\"tokens_remaining\":" << result.tokens_remaining
        << ",\"physical_mass_exhaust\":"
        << result.physical_mass_exhaust
        << ",\"informational_energy_measured\":"
        << (result.informational_energy_measured ? "true" : "false")
        << ",\"energy_residual\":" << result.energy_residual << "}\n";
}

void WriteTrace(const RunResult& result) {
    WriteAssembly("complete", result.complete);
    WriteAssembly("tile_01_starvation", result.starved);
    WriteAssembly("activation_token_limit", result.token_limited);
    WriteSafety("duplicate_site_contention", result.duplicate);
    WriteSafety("filled_site_overwrite", result.overwrite);
    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"seeded_xor_tile_assembly\","
        << "\"model\":\"abstract_tile_assembly\","
        << "\"lattice\":\"4x4\","
        << "\"rule\":\"west_xor_south\","
        << "\"tokens_consumed_equal_sites_filled\":true,"
        << "\"state_monotonic\":true,"
        << "\"molecular_dynamics\":false,"
        << "\"kinetics_modeled\":false,"
        << "\"pathway_optimization\":false,"
        << "\"deterministic_replay\":true,"
        << "\"oracle_verified\":true}\n";
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
