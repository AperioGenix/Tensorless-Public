#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr uint32_t kExtent = 19;
constexpr uint32_t kTicks = 8;
constexpr uint64_t kInitialMass = 1679616ULL;  // 6^8

uint32_t Wrap(int32_t coordinate) {
    const int32_t remainder = coordinate % static_cast<int32_t>(kExtent);
    return static_cast<uint32_t>(
        remainder < 0 ? remainder + static_cast<int32_t>(kExtent) :
                        remainder);
}

uint64_t Index(uint32_t x, uint32_t y, uint32_t z) {
    return tensorless_adapter::Flat(kExtent, kExtent, x, y, z);
}

bool AdvanceOracle(
    const std::vector<uint64_t>& current,
    std::vector<uint64_t>& next,
    uint64_t divisor) {
    next.assign(current.size(), 0);
    const int32_t offsets[6][3]{
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0},
        {0, 1, 0}, {0, 0, -1}, {0, 0, 1}
    };
    for (uint32_t x = 0; x < kExtent; ++x)
        for (uint32_t y = 0; y < kExtent; ++y)
            for (uint32_t z = 0; z < kExtent; ++z) {
                const uint64_t value = current[Index(x, y, z)];
                if (value == 0) continue;
                if (value % divisor != 0) return false;
                const uint64_t share = value / divisor;
                for (const auto& offset : offsets) {
                    const uint64_t target = Index(
                        Wrap(static_cast<int32_t>(x) + offset[0]),
                        Wrap(static_cast<int32_t>(y) + offset[1]),
                        Wrap(static_cast<int32_t>(z) + offset[2]));
                    next[target] += share;
                }
            }
    return true;
}
}

int main() {
    const uint64_t count = uint64_t(kExtent) * kExtent * kExtent;
    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(kExtent, kExtent, kExtent);
    if (!sandbox.Create(info)) return 2;

    const uint32_t center = kExtent / 2;
    const uint64_t center_index = Index(center, center, center);
    TensorlessFpmSandboxNode center_node{};
    center_node.energy_subunits = kInitialMass;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), center_index, &center_node),
            "Tensorless_FpmSandboxSetNode"))
        return 2;

    std::vector<uint64_t> oracle(static_cast<size_t>(count), 0);
    std::vector<uint64_t> next_oracle;
    oracle[center_index] = kInitialMass;
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    std::vector<TensorlessFpmSandboxNode> nodes;
    TensorlessFpmSandboxStats stats{};

    for (uint32_t tick = 0; tick < kTicks; ++tick) {
        if (!tensorless_adapter::ReadNodes(sandbox.get(), nodes)) return 2;
        for (uint64_t index = 0; index < count; ++index)
            payloads[static_cast<size_t>(index)] =
                nodes[static_cast<size_t>(index)].energy_subunits;
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxStep(
                    sandbox.get(), actions.data(), payloads.data(),
                    count, &stats),
                "Tensorless_FpmSandboxStep"))
            return 2;
        if (!AdvanceOracle(oracle, next_oracle, 6)) {
            std::cerr << "oracle lost divisibility at tick " << tick << "\n";
            return 3;
        }
        oracle.swap(next_oracle);
        if (!tensorless_adapter::ReadNodes(sandbox.get(), nodes)) return 2;
        for (uint64_t index = 0; index < count; ++index) {
            if (nodes[static_cast<size_t>(index)].energy_subunits !=
                oracle[static_cast<size_t>(index)]) {
                std::cerr << "diffusion mismatch at tick " << tick + 1
                          << " index " << index << "\n";
                return 4;
            }
        }
        if (!tensorless_adapter::ResidualsAreZero(stats)) {
            std::cerr << "nonzero conservation residual at tick "
                      << tick + 1 << "\n";
            return 5;
        }

        uint64_t active_nodes = 0;
        uint64_t maximum_mass = 0;
        for (const TensorlessFpmSandboxNode& node : nodes) {
            if (node.energy_subunits != 0) ++active_nodes;
            if (node.energy_subunits > maximum_mass)
                maximum_mass = node.energy_subunits;
        }
        std::cout
            << "{\"type\":\"frame\",\"tick\":" << tick + 1
            << ",\"active_nodes\":" << active_nodes
            << ",\"maximum_mass\":" << maximum_mass
            << ",\"z_slice\":[";
        bool first = true;
        for (uint32_t x = 0; x < kExtent; ++x)
            for (uint32_t y = 0; y < kExtent; ++y) {
                const uint64_t mass =
                    nodes[Index(x, y, center)].energy_subunits;
                if (mass == 0) continue;
                std::cout << (first ? "" : ",")
                          << "[" << x << "," << y << "," << mass << "]";
                first = false;
            }
        std::cout << "]}\n";
    }

    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"discrete_diffusion\","
        << "\"grid\":[19,19,19],"
        << "\"ticks\":8,"
        << "\"initial_mass\":1679616,"
        << "\"matched_nodes\":" << count << ","
        << "\"energy_residual\":0}\n";
    return 0;
}
