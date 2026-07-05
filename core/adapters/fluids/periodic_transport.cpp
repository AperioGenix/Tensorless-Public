#include "../common/sandbox_adapter.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr uint32_t kExtent = 7;
constexpr uint32_t kBlockWidth = 3;
constexpr uint32_t kTicks = 10;
constexpr uint64_t kCellMass = 1000;

uint64_t Index(uint32_t x, uint32_t y, uint32_t z) {
    return tensorless_adapter::Flat(kExtent, kExtent, x, y, z);
}

bool ExpectedOccupied(
    uint32_t x, uint32_t y, uint32_t z, uint32_t tick) {
    const uint32_t start_x = (1U + tick) % kExtent;
    bool x_match = false;
    for (uint32_t offset = 0; offset < kBlockWidth; ++offset)
        if (x == (start_x + offset) % kExtent) x_match = true;
    return x_match &&
        y >= 2 && y < 2 + kBlockWidth &&
        z >= 2 && z < 2 + kBlockWidth;
}
}

int main() {
    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(
            kExtent, kExtent, kExtent);
    if (!sandbox.Create(info)) return 2;

    for (uint32_t x = 1; x < 1 + kBlockWidth; ++x)
        for (uint32_t y = 2; y < 2 + kBlockWidth; ++y)
            for (uint32_t z = 2; z < 2 + kBlockWidth; ++z) {
                TensorlessFpmSandboxNode node{};
                node.energy_subunits = kCellMass;
                node.momentum[0][0] =
                    static_cast<int64_t>(kCellMass);
                if (!tensorless_adapter::Check(
                        Tensorless_FpmSandboxSetNode(
                            sandbox.get(), Index(x, y, z), &node),
                        "Tensorless_FpmSandboxSetNode"))
                    return 2;
            }

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
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
        if (!tensorless_adapter::ReadNodes(sandbox.get(), nodes)) return 2;

        for (uint32_t x = 0; x < kExtent; ++x)
            for (uint32_t y = 0; y < kExtent; ++y)
                for (uint32_t z = 0; z < kExtent; ++z) {
                    const TensorlessFpmSandboxNode& node =
                        nodes[Index(x, y, z)];
                    const bool occupied =
                        ExpectedOccupied(x, y, z, tick + 1);
                    const uint64_t expected_energy =
                        occupied ? kCellMass : 0;
                    const int64_t expected_momentum =
                        occupied ? static_cast<int64_t>(kCellMass) : 0;
                    if (node.energy_subunits != expected_energy ||
                        node.momentum[0][0] != expected_momentum) {
                        std::cerr
                            << "periodic transport mismatch at tick "
                            << tick + 1 << " coordinate ("
                            << x << "," << y << "," << z << ")\n";
                        return 3;
                    }
                    for (uint32_t row = 0; row < 3; ++row)
                        for (uint32_t column = 0; column < 3; ++column)
                            if ((row != 0 || column != 0) &&
                                node.momentum[row][column] != 0) {
                                std::cerr
                                    << "unexpected momentum channel at tick "
                                    << tick + 1 << "\n";
                                return 3;
                            }
                }
        if (!tensorless_adapter::ResidualsAreZero(stats) ||
            stats.external_energy_exhaust_subunits != 0) {
            std::cerr << "transport ledger mismatch at tick "
                      << tick + 1 << "\n";
            return 4;
        }
        std::cout
            << "{\"type\":\"frame\",\"tick\":" << tick + 1
            << ",\"start_x\":" << (1U + tick + 1U) % kExtent
            << ",\"occupied_cells\":27}\n";
    }

    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"periodic_transport\","
        << "\"grid\":[7,7,7],"
        << "\"block\":[3,3,3],"
        << "\"ticks\":10,"
        << "\"cell_mass\":1000,"
        << "\"energy_residual\":0,"
        << "\"momentum_residual\":0}\n";
    return 0;
}
