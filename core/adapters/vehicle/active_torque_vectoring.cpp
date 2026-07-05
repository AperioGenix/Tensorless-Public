#include "../common/sandbox_adapter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
constexpr uint32_t kSizeX = 3;
constexpr uint32_t kSizeY = 3;
constexpr uint64_t kTotalForceDemand = 14000;
constexpr int64_t kYawMomentTarget = 6000;
constexpr uint64_t kHalfTrack = 1;

struct TireInput {
    const char* name;
    uint32_t x;
    uint32_t y;
    uint64_t friction_limit;
    uint64_t lateral_force;
    uint64_t expected_longitudinal_capacity;
};

constexpr std::array<TireInput, 2> kTires{{
    {"rear_left", 2, 1, 5000, 4000, 3000},
    {"rear_right", 1, 2, 15000, 12000, 9000},
}};

struct ControllerCommand {
    uint64_t rear_left = 0;
    uint64_t rear_right = 0;
    uint64_t admitted_total = 0;
    uint64_t upstream_holdback = 0;
    int64_t achieved_yaw_moment = 0;

    bool operator==(const ControllerCommand&) const = default;
};

struct CoreTransactionResult {
    std::array<uint64_t, kTires.size()> delivered{};
    uint64_t engine_retained = 0;
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const CoreTransactionResult&) const = default;
};

struct RunResult {
    ControllerCommand command{};
    std::array<uint64_t, kTires.size()> capacities{};
    CoreTransactionResult controlled{};
    CoreTransactionResult safety{};

    bool operator==(const RunResult&) const = default;
};

uint64_t IntegerSqrt(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = uint64_t{1} << 62U;
    while (bit > value) bit >>= 2U;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1U) + bit;
        } else {
            result >>= 1U;
        }
        bit >>= 2U;
    }
    return result;
}

bool LongitudinalCapacity(
    uint64_t friction_limit, uint64_t lateral_force,
    uint64_t& capacity) {
    if (lateral_force > friction_limit)
        return false;
    if (friction_limit != 0 &&
        friction_limit > std::numeric_limits<uint64_t>::max() /
            friction_limit)
        return false;
    if (lateral_force != 0 &&
        lateral_force > std::numeric_limits<uint64_t>::max() /
            lateral_force)
        return false;
    const uint64_t remaining_squared =
        friction_limit * friction_limit -
        lateral_force * lateral_force;
    capacity = IntegerSqrt(remaining_squared);
    if (capacity == 0)
        return remaining_squared == 0;
    return capacity <=
               std::numeric_limits<uint64_t>::max() / capacity &&
           capacity * capacity == remaining_squared;
}

bool ComputeControllerCommand(
    uint64_t total_force, int64_t yaw_moment,
    uint64_t half_track,
    uint64_t rear_left_capacity,
    uint64_t rear_right_capacity,
    ControllerCommand& command) {
    if (half_track == 0 ||
        half_track >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()) ||
        rear_left_capacity >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max() / 2) ||
        rear_right_capacity >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max() / 2) ||
        total_force >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()))
        return false;

    const int64_t signed_half_track =
        static_cast<int64_t>(half_track);
    if (yaw_moment % signed_half_track != 0)
        return false;
    const int64_t difference = yaw_moment / signed_half_track;
    if (difference == std::numeric_limits<int64_t>::min())
        return false;
    const int64_t absolute_difference =
        difference < 0 ? -difference : difference;

    const int64_t doubled_left =
        2 * static_cast<int64_t>(rear_left_capacity);
    const int64_t doubled_right =
        2 * static_cast<int64_t>(rear_right_capacity);
    if ((difference > 0 &&
         doubled_left >
             std::numeric_limits<int64_t>::max() - difference) ||
        (difference < 0 &&
         doubled_right >
             std::numeric_limits<int64_t>::max() +
                 difference))
        return false;
    const int64_t left_bound = doubled_left + difference;
    const int64_t right_bound = doubled_right - difference;
    if (left_bound < absolute_difference ||
        right_bound < absolute_difference)
        return false;

    int64_t admitted = std::min({
        static_cast<int64_t>(total_force),
        left_bound,
        right_bound,
    });
    if ((admitted - difference) % 2 != 0)
        --admitted;
    if (admitted < absolute_difference)
        return false;

    const int64_t rear_left = (admitted - difference) / 2;
    const int64_t rear_right = (admitted + difference) / 2;
    if (rear_left < 0 || rear_right < 0 ||
        rear_left >
            static_cast<int64_t>(rear_left_capacity) ||
        rear_right >
            static_cast<int64_t>(rear_right_capacity))
        return false;

    command.rear_left = static_cast<uint64_t>(rear_left);
    command.rear_right = static_cast<uint64_t>(rear_right);
    command.admitted_total = static_cast<uint64_t>(admitted);
    command.upstream_holdback =
        total_force - command.admitted_total;
    command.achieved_yaw_moment =
        signed_half_track * (rear_right - rear_left);
    return command.achieved_yaw_moment == yaw_moment;
}

bool CheckControllerCases() {
    struct Case {
        uint64_t total;
        int64_t yaw;
        uint64_t half_track;
        uint64_t left_capacity;
        uint64_t right_capacity;
        bool feasible;
        ControllerCommand expected;
    };
    constexpr std::array<Case, 5> cases{{
        {6000, 0, 1, 3000, 9000, true,
         {3000, 3000, 6000, 0, 0}},
        {8000, -2000, 1, 3000, 9000, true,
         {3000, 1000, 4000, 4000, -2000}},
        {11, 2, 1, 10, 10, true,
         {4, 6, 10, 1, 2}},
        {14000, 1, 2, 3000, 9000, false, {}},
        {14000, 10000, 1, 3000, 9000, false, {}},
    }};

    for (const Case& test : cases) {
        ControllerCommand actual{};
        const bool feasible = ComputeControllerCommand(
            test.total, test.yaw, test.half_track,
            test.left_capacity, test.right_capacity, actual);
        if (feasible != test.feasible ||
            (feasible && !(actual == test.expected)))
            return false;
    }
    return true;
}

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(kSizeY, 1, x, y, 0);
}

bool ConfigureEngine(
    TensorlessFpmSandbox* sandbox,
    uint64_t initial_energy,
    uint64_t rear_left_weight,
    uint64_t rear_right_weight) {
    if (initial_energy >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()) ||
        rear_left_weight >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()) ||
        rear_right_weight >
            static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()))
        return false;
    TensorlessFpmSandboxNode engine{};
    engine.energy_subunits = initial_energy;
    engine.momentum[0][0] =
        static_cast<int64_t>(rear_left_weight);
    engine.momentum[1][1] =
        static_cast<int64_t>(rear_right_weight);
    return tensorless_adapter::Check(
        Tensorless_FpmSandboxSetNode(
            sandbox, Index(1, 1), &engine),
        "Tensorless_FpmSandboxSetNode(engine)");
}

bool ExecuteCoreTransaction(
    const std::array<uint64_t, kTires.size()>& capacities,
    uint64_t rear_left_weight,
    uint64_t rear_right_weight,
    uint64_t submitted_payload,
    CoreTransactionResult& result) {
    if (rear_left_weight >
            std::numeric_limits<uint64_t>::max() -
                rear_right_weight ||
        rear_left_weight + rear_right_weight != submitted_payload ||
        submitted_payload > kTotalForceDemand) {
        std::cerr << "invalid driveline command\n";
        return false;
    }

    tensorless_adapter::Sandbox sandbox;
    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(
            kSizeX, kSizeY, 1);
    info.isotropic_face_weight = 0;
    if (!sandbox.Create(info) ||
        !ConfigureEngine(
            sandbox.get(), kTotalForceDemand,
            rear_left_weight, rear_right_weight))
        return false;

    std::array<uint64_t, kTires.size()> initial_tire_energy{};
    for (size_t index = 0; index < kTires.size(); ++index) {
        TensorlessFpmSandboxNode tire{};
        tire.energy_subunits =
            tensorless_adapter::kEnergyCeiling -
            capacities[index];
        initial_tire_energy[index] = tire.energy_subunits;
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(),
                    Index(kTires[index].x, kTires[index].y),
                    &tire),
                "Tensorless_FpmSandboxSetNode(tire)"))
            return false;
    }

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    payloads[Index(1, 1)] = submitted_payload;

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep"))
        return false;

    TensorlessFpmSandboxNode engine{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxGetNode(
                sandbox.get(), Index(1, 1), &engine),
            "Tensorless_FpmSandboxGetNode(engine)"))
        return false;

    result = {};
    result.engine_retained = engine.energy_subunits;
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];

    for (size_t index = 0; index < kTires.size(); ++index) {
        TensorlessFpmSandboxNode tire{};
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxGetNode(
                    sandbox.get(),
                    Index(kTires[index].x, kTires[index].y),
                    &tire),
                "Tensorless_FpmSandboxGetNode(tire)"))
            return false;
        result.delivered[index] =
            tire.energy_subunits - initial_tire_energy[index];
    }

    if (!tensorless_adapter::ResidualsAreZero(stats)) {
        std::cerr << "core transaction has a conservation residual\n";
        return false;
    }
    return true;
}

bool Execute(RunResult& result) {
    std::array<uint64_t, kTires.size()> capacities{};
    for (size_t index = 0; index < kTires.size(); ++index) {
        if (!LongitudinalCapacity(
                kTires[index].friction_limit,
                kTires[index].lateral_force,
                capacities[index]) ||
            capacities[index] !=
                kTires[index].expected_longitudinal_capacity) {
            std::cerr << "friction-circle capacity mismatch\n";
            return false;
        }
    }

    ControllerCommand command{};
    if (!ComputeControllerCommand(
            kTotalForceDemand, kYawMomentTarget, kHalfTrack,
            capacities[0], capacities[1], command)) {
        std::cerr << "controller found no exact feasible split\n";
        return false;
    }

    const ControllerCommand controller_oracle{
        3000, 9000, 12000, 2000, 6000};
    if (!(command == controller_oracle)) {
        std::cerr << "controller differs from closed-form oracle\n";
        return false;
    }

    result = {};
    result.command = command;
    result.capacities = capacities;
    if (!ExecuteCoreTransaction(
            capacities, command.rear_left, command.rear_right,
            command.admitted_total, result.controlled) ||
        !ExecuteCoreTransaction(
            capacities, 7000, 7000, kTotalForceDemand,
            result.safety))
        return false;

    const CoreTransactionResult controlled_oracle{
        {3000, 9000}, 2000, 0, 0, {}};
    const CoreTransactionResult safety_oracle{
        {3000, 7000}, 0, 4000, 0, {}};
    if (!(result.controlled == controlled_oracle) ||
        !(result.safety == safety_oracle)) {
        std::cerr << "core transaction differs from oracle\n";
        return false;
    }
    return true;
}

void WriteTrace(const RunResult& result) {
    std::cout
        << "{\"type\":\"controller\","
        << "\"total_force_demand\":" << kTotalForceDemand << ","
        << "\"yaw_moment_target\":" << kYawMomentTarget << ","
        << "\"half_track\":" << kHalfTrack << ","
        << "\"rear_left_command\":"
        << result.command.rear_left << ","
        << "\"rear_right_command\":"
        << result.command.rear_right << ","
        << "\"admitted_total\":"
        << result.command.admitted_total << ","
        << "\"upstream_holdback\":"
        << result.command.upstream_holdback << ","
        << "\"achieved_yaw_moment\":"
        << result.command.achieved_yaw_moment << "}\n";

    for (size_t index = 0; index < kTires.size(); ++index) {
        std::cout
            << "{\"type\":\"wheel\","
            << "\"wheel\":\"" << kTires[index].name << "\","
            << "\"friction_limit\":"
            << kTires[index].friction_limit << ","
            << "\"lateral_force\":"
            << kTires[index].lateral_force << ","
            << "\"longitudinal_capacity\":"
            << result.capacities[index] << ","
            << "\"commanded_longitudinal_force\":"
            << (index == 0
                    ? result.command.rear_left
                    : result.command.rear_right)
            << ",\"delivered_longitudinal_force\":"
            << result.controlled.delivered[index] << "}\n";
    }

    std::cout
        << "{\"type\":\"safety_case\","
        << "\"mode\":\"ecu_failure_equal_split\","
        << "\"rear_left_request\":7000,"
        << "\"rear_right_request\":7000,"
        << "\"rear_left_delivered\":"
        << result.safety.delivered[0] << ","
        << "\"rear_right_delivered\":"
        << result.safety.delivered[1] << ","
        << "\"engine_retained\":"
        << result.safety.engine_retained << ","
        << "\"external_exhaust\":"
        << result.safety.external_exhaust << ","
        << "\"energy_residual\":"
        << result.safety.energy_residual << "}\n";

    std::cout
        << "{\"type\":\"summary\","
        << "\"adapter\":\"active_torque_vectoring\","
        << "\"payload_unit\":\"longitudinal_force_subunit\","
        << "\"engine_retained\":"
        << result.controlled.engine_retained << ","
        << "\"external_exhaust\":"
        << result.controlled.external_exhaust << ","
        << "\"energy_residual\":"
        << result.controlled.energy_residual << ","
        << "\"safety_case_external_exhaust\":"
        << result.safety.external_exhaust << ","
        << "\"momentum_residual_zero\":true,"
        << "\"oracle_verified\":true,"
        << "\"deterministic_replay\":true}\n";
}
}  // namespace

int main() {
    static_assert(3000ULL * 3000ULL + 4000ULL * 4000ULL ==
                  5000ULL * 5000ULL);
    static_assert(9000ULL * 9000ULL + 12000ULL * 12000ULL ==
                  15000ULL * 15000ULL);

    if (!CheckControllerCases()) {
        std::cerr << "controller case matrix failed\n";
        return 2;
    }

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
