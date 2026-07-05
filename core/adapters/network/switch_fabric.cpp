#include "../common/sandbox_adapter.h"

#include <cstdint>
#include <deque>
#include <iostream>
#include <utility>
#include <vector>

namespace {
constexpr uint32_t kExtent = 3;
constexpr uint32_t kTicks = 20;
constexpr uint64_t kServiceCapacity = 5;

struct ArbitrationResult {
    uint64_t accepted_x = 0;
    uint64_t accepted_y = 0;
    uint64_t rejected_x = 0;
    uint64_t rejected_y = 0;
};

struct Frame {
    uint32_t tick = 0;
    uint64_t arrivals_x = 0;
    uint64_t arrivals_y = 0;
    uint64_t offered_x = 0;
    uint64_t offered_y = 0;
    uint64_t accepted_x = 0;
    uint64_t accepted_y = 0;
    uint64_t remaining_x = 0;
    uint64_t remaining_y = 0;

    bool operator==(const Frame&) const = default;
};

struct RunResult {
    uint64_t arrivals = 0;
    uint64_t departures = 0;
    uint64_t queue_area = 0;
    uint64_t wait_sum = 0;
    uint64_t final_queue = 0;
    std::vector<std::pair<uint64_t,uint64_t>> accepted;
    std::vector<Frame> frames;
};

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(kExtent, 1, x, y, 0);
}

std::pair<uint64_t,uint64_t> OracleAllocation(
    uint64_t offer_x, uint64_t offer_y) {
    const uint64_t total = offer_x + offer_y;
    if (total <= kServiceCapacity) return {offer_x, offer_y};
    uint64_t accepted_x = kServiceCapacity * offer_x / total;
    uint64_t accepted_y = kServiceCapacity * offer_y / total;
    const uint64_t remainder_x =
        kServiceCapacity * offer_x % total;
    const uint64_t remainder_y =
        kServiceCapacity * offer_y % total;
    uint64_t remaining =
        kServiceCapacity - accepted_x - accepted_y;
    while (remaining != 0) {
        const bool choose_x =
            remainder_x > remainder_y ||
            (remainder_x == remainder_y && offer_x >= offer_y);
        if (choose_x) ++accepted_x;
        else ++accepted_y;
        --remaining;
    }
    return {accepted_x, accepted_y};
}

bool SetSource(
    TensorlessFpmSandbox* sandbox, uint64_t index,
    uint64_t offered, uint32_t axis) {
    TensorlessFpmSandboxNode node{};
    node.energy_subunits = offered;
    node.momentum[axis][axis] = static_cast<int64_t>(offered);
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(sandbox, index, &node),
        "Tensorless_FpmSandboxSetNode(source)");
}

bool Arbitrate(
    uint64_t offer_x, uint64_t offer_y,
    ArbitrationResult& result) {
    tensorless_adapter::Sandbox sandbox;
    const TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::DirectionalConfig(kExtent, kExtent, 1);
    if (!sandbox.Create(info)) return false;

    const uint64_t source_x = Index(0, 1);
    const uint64_t source_y = Index(1, 0);
    const uint64_t sink = Index(1, 1);
    if (!SetSource(sandbox.get(), source_x, offer_x, 0) ||
        !SetSource(sandbox.get(), source_y, offer_y, 1))
        return false;
    TensorlessFpmSandboxNode sink_node{};
    sink_node.energy_subunits =
        tensorless_adapter::kEnergyCeiling - kServiceCapacity;
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), sink, &sink_node),
            "Tensorless_FpmSandboxSetNode(sink)"))
        return false;

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[source_x] = offer_x;
    payloads[source_y] = offer_y;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep"))
        return false;

    TensorlessFpmSandboxNode sink_result{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), sink, &sink_result),
            "Tensorless_FpmSandboxGetNode(sink)"))
        return false;
    result.accepted_x =
        static_cast<uint64_t>(sink_result.momentum[0][0]);
    result.accepted_y =
        static_cast<uint64_t>(sink_result.momentum[1][1]);
    result.rejected_x = static_cast<uint64_t>(
        stats.external_momentum_exhaust[0][0]);
    result.rejected_y = static_cast<uint64_t>(
        stats.external_momentum_exhaust[1][1]);

    const auto oracle = OracleAllocation(offer_x, offer_y);
    if (result.accepted_x != oracle.first ||
        result.accepted_y != oracle.second ||
        result.rejected_x != offer_x - oracle.first ||
        result.rejected_y != offer_y - oracle.second ||
        stats.external_energy_exhaust_subunits !=
            result.rejected_x + result.rejected_y ||
        !tensorless_adapter::ResidualsAreZero(stats)) {
        std::cerr << "switch arbitration mismatch for offers ("
                  << offer_x << "," << offer_y << ")\n";
        return false;
    }
    return true;
}

bool RunWorkload(RunResult& result) {
    std::deque<uint32_t> queue_x;
    std::deque<uint32_t> queue_y;
    result = {};
    for (uint32_t tick = 0; tick < kTicks; ++tick) {
        const uint64_t arrivals_x = tick % 2U == 0 ? 5 : 0;
        const uint64_t arrivals_y = tick % 2U == 0 ? 3 : 0;
        if (tick % 2U == 0) {
            for (uint32_t token = 0; token < 5; ++token)
                queue_x.push_back(tick);
            for (uint32_t token = 0; token < 3; ++token)
                queue_y.push_back(tick);
            result.arrivals += 8;
        }
        result.queue_area += queue_x.size() + queue_y.size();

        ArbitrationResult arbitration{};
        if (!Arbitrate(
                queue_x.size(), queue_y.size(), arbitration))
            return false;
        result.accepted.push_back({
            arbitration.accepted_x, arbitration.accepted_y});
        if (arbitration.accepted_x + arbitration.accepted_y >
            kServiceCapacity)
            return false;

        for (uint64_t count = 0;
             count < arbitration.accepted_x; ++count) {
            if (queue_x.empty()) return false;
            result.wait_sum += tick - queue_x.front() + 1U;
            queue_x.pop_front();
            ++result.departures;
        }
        for (uint64_t count = 0;
             count < arbitration.accepted_y; ++count) {
            if (queue_y.empty()) return false;
            result.wait_sum += tick - queue_y.front() + 1U;
            queue_y.pop_front();
            ++result.departures;
        }
        result.frames.push_back({
            tick + 1,
            arrivals_x,
            arrivals_y,
            arbitration.accepted_x + arbitration.rejected_x,
            arbitration.accepted_y + arbitration.rejected_y,
            arbitration.accepted_x,
            arbitration.accepted_y,
            queue_x.size(),
            queue_y.size(),
        });
        if (result.arrivals !=
            result.departures + queue_x.size() + queue_y.size())
            return false;
    }
    result.final_queue = queue_x.size() + queue_y.size();
    return result.final_queue == 0 &&
        result.arrivals == result.departures &&
        result.queue_area * result.departures ==
            result.arrivals * result.wait_sum;
}
}

int main() {
    RunResult first{}, second{};
    if (!RunWorkload(first) || !RunWorkload(second)) {
        std::cerr << "switch workload invariant failed\n";
        return 2;
    }
    if (first.arrivals != second.arrivals ||
        first.departures != second.departures ||
        first.queue_area != second.queue_area ||
        first.wait_sum != second.wait_sum ||
        first.accepted != second.accepted ||
        first.frames != second.frames) {
        std::cerr << "switch workload replay mismatch\n";
        return 3;
    }
    if (first.arrivals != 80 ||
        first.departures != 80 ||
        first.queue_area != 110 ||
        first.wait_sum != 110) {
        std::cerr << "unexpected switch workload totals\n";
        return 4;
    }

    for (const Frame& frame : first.frames)
        std::cout
            << "{\"type\":\"frame\",\"tick\":" << frame.tick
            << ",\"arrivals\":[" << frame.arrivals_x << ","
            << frame.arrivals_y << "]"
            << ",\"offered\":[" << frame.offered_x << ","
            << frame.offered_y << "]"
            << ",\"accepted\":[" << frame.accepted_x << ","
            << frame.accepted_y << "]"
            << ",\"remaining\":[" << frame.remaining_x << ","
            << frame.remaining_y << "]}\n";
    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"switch_fabric\","
        << "\"ticks\":20,"
        << "\"service_capacity\":5,"
        << "\"arrivals\":80,"
        << "\"departures\":80,"
        << "\"queue_area\":110,"
        << "\"wait_sum\":110,"
        << "\"mean_queue\":\"110/20\","
        << "\"arrival_rate\":\"80/20\","
        << "\"mean_wait\":\"110/80\","
        << "\"little_law\":true,"
        << "\"deterministic_replay\":true}\n";
    return 0;
}
