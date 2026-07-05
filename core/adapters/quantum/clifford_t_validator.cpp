#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {
constexpr uint32_t kMaxQubits = 8;
constexpr uint64_t kShots = 10000;
constexpr uint64_t kSeed = UINT64_C(0xC11FF04D7E570123);
constexpr uint64_t kSqrt2Q32 = UINT64_C(6074000999);
constexpr uint64_t kQ32Scale = UINT64_C(1) << 32U;

class SplitMix64 {
public:
    explicit SplitMix64(uint64_t seed) : state_(seed) {}

    uint64_t Next() {
        uint64_t value = (state_ += UINT64_C(0x9E3779B97F4A7C15));
        value = (value ^ (value >> 30U)) *
                UINT64_C(0xBF58476D1CE4E5B9);
        value = (value ^ (value >> 27U)) *
                UINT64_C(0x94D049BB133111EB);
        return value ^ (value >> 31U);
    }

private:
    uint64_t state_;
};

struct Cyclotomic {
    // c[0] + c[1]w + c[2]w^2 + c[3]w^3, where w^4 = -1.
    std::array<int64_t, 4> c{};

    bool operator==(const Cyclotomic&) const = default;
};

Cyclotomic Add(const Cyclotomic& left, const Cyclotomic& right) {
    Cyclotomic result{};
    for (uint32_t index = 0; index < 4; ++index)
        result.c[index] = left.c[index] + right.c[index];
    return result;
}

Cyclotomic Subtract(const Cyclotomic& left, const Cyclotomic& right) {
    Cyclotomic result{};
    for (uint32_t index = 0; index < 4; ++index)
        result.c[index] = left.c[index] - right.c[index];
    return result;
}

Cyclotomic Multiply(const Cyclotomic& left, const Cyclotomic& right) {
    std::array<int64_t, 7> product{};
    for (uint32_t left_index = 0; left_index < 4; ++left_index)
        for (uint32_t right_index = 0; right_index < 4; ++right_index)
            product[left_index + right_index] +=
                left.c[left_index] * right.c[right_index];
    Cyclotomic result{};
    for (uint32_t exponent = 0; exponent < product.size(); ++exponent) {
        if (exponent < 4)
            result.c[exponent] += product[exponent];
        else
            result.c[exponent - 4] -= product[exponent];
    }
    return result;
}

Cyclotomic MultiplyOmega(const Cyclotomic& value) {
    return {{
        -value.c[3],
        value.c[0],
        value.c[1],
        value.c[2],
    }};
}

Cyclotomic Conjugate(const Cyclotomic& value) {
    return {{
        value.c[0],
        -value.c[3],
        -value.c[2],
        -value.c[1],
    }};
}

struct ExactProbability {
    // (integer + sqrt2_coefficient * sqrt(2)) / 2^denominator_power.
    int64_t integer = 0;
    int64_t sqrt2_coefficient = 0;
    uint32_t denominator_power = 0;

    bool operator==(const ExactProbability&) const = default;
};

std::optional<ExactProbability> NormSquared(
    const Cyclotomic& value,
    uint32_t denominator_power) {
    const Cyclotomic norm = Multiply(value, Conjugate(value));
    if (norm.c[2] != 0 || norm.c[3] != -norm.c[1])
        return std::nullopt;
    return ExactProbability{
        norm.c[0],
        norm.c[1],
        denominator_power,
    };
}

class CliffordTState {
public:
    explicit CliffordTState(uint32_t qubits)
        : qubits_(qubits),
          amplitudes_(
              qubits <= kMaxQubits
                  ? static_cast<size_t>(UINT64_C(1) << qubits)
                  : 0) {
        if (!amplitudes_.empty())
            amplitudes_[0].c[0] = 1;
    }

    bool valid() const {
        return !amplitudes_.empty();
    }

    uint32_t qubits() const {
        return qubits_;
    }

    uint32_t denominator_power() const {
        return denominator_power_;
    }

    size_t amplitude_count() const {
        return amplitudes_.size();
    }

    void ApplyH(uint32_t qubit) {
        const uint64_t mask = BitMask(qubit);
        for (uint64_t basis = 0; basis < amplitudes_.size(); ++basis) {
            if ((basis & mask) != 0)
                continue;
            const uint64_t one = basis | mask;
            const Cyclotomic zero_value = amplitudes_[basis];
            const Cyclotomic one_value = amplitudes_[one];
            amplitudes_[basis] = Add(zero_value, one_value);
            amplitudes_[one] = Subtract(zero_value, one_value);
        }
        ++denominator_power_;
    }

    void ApplyT(uint32_t qubit) {
        const uint64_t mask = BitMask(qubit);
        for (uint64_t basis = 0; basis < amplitudes_.size(); ++basis)
            if ((basis & mask) != 0)
                amplitudes_[basis] =
                    MultiplyOmega(amplitudes_[basis]);
    }

    void ApplyS(uint32_t qubit) {
        ApplyT(qubit);
        ApplyT(qubit);
    }

    void ApplyZ(uint32_t qubit) {
        const uint64_t mask = BitMask(qubit);
        for (uint64_t basis = 0; basis < amplitudes_.size(); ++basis)
            if ((basis & mask) != 0)
                for (int64_t& coefficient : amplitudes_[basis].c)
                    coefficient = -coefficient;
    }

    void ApplyX(uint32_t qubit) {
        const uint64_t mask = BitMask(qubit);
        for (uint64_t basis = 0; basis < amplitudes_.size(); ++basis) {
            if ((basis & mask) == 0)
                std::swap(amplitudes_[basis], amplitudes_[basis | mask]);
        }
    }

    void ApplyCnot(uint32_t control, uint32_t target) {
        const uint64_t control_mask = BitMask(control);
        const uint64_t target_mask = BitMask(target);
        for (uint64_t basis = 0; basis < amplitudes_.size(); ++basis) {
            if ((basis & control_mask) != 0 &&
                (basis & target_mask) == 0)
                std::swap(
                    amplitudes_[basis],
                    amplitudes_[basis | target_mask]);
        }
    }

    std::optional<ExactProbability> OutcomeProbability(
        uint64_t outcome) const {
        if (outcome >= amplitudes_.size())
            return std::nullopt;
        return NormSquared(
            amplitudes_[outcome],
            denominator_power_);
    }

    bool operator==(const CliffordTState&) const = default;

private:
    uint64_t BitMask(uint32_t qubit) const {
        return UINT64_C(1) << (qubits_ - 1U - qubit);
    }

    uint32_t qubits_;
    uint32_t denominator_power_ = 0;
    std::vector<Cyclotomic> amplitudes_;
};

std::optional<uint64_t> ProbabilityToQ32(
    const ExactProbability& probability) {
    if (probability.denominator_power >= 63)
        return std::nullopt;
    const int64_t scaled =
        probability.integer * static_cast<int64_t>(kQ32Scale) +
        probability.sqrt2_coefficient *
            static_cast<int64_t>(kSqrt2Q32);
    if (scaled < 0)
        return std::nullopt;
    const uint64_t denominator =
        UINT64_C(1) << probability.denominator_power;
    const uint64_t threshold =
        static_cast<uint64_t>(scaled) / denominator;
    if (threshold > kQ32Scale)
        return std::nullopt;
    return threshold;
}

std::array<uint64_t, 2> SampleBinary(
    uint64_t outcome_zero_threshold_q32,
    uint64_t shots,
    uint64_t seed) {
    SplitMix64 random(seed);
    std::array<uint64_t, 2> counts{};
    for (uint64_t shot = 0; shot < shots; ++shot) {
        const uint64_t sample = random.Next() >> 32U;
        ++counts[sample < outcome_zero_threshold_q32 ? 0 : 1];
    }
    return counts;
}

struct RoutingResult {
    std::array<uint64_t, 2> accepted{};
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const RoutingResult&) const = default;
};

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(3, 1, x, y, 0);
}

bool RouteCounts(
    const std::array<uint64_t, 2>& requested,
    const std::array<uint64_t, 2>& capacities,
    RoutingResult& result) {
    if (requested[0] + requested[1] != kShots)
        return false;
    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(3, 3, 1);
    info.isotropic_face_weight = 0;
    tensorless_adapter::Sandbox sandbox;
    if (!sandbox.Create(info))
        return false;

    const uint64_t source = Index(1, 1);
    const std::array<uint64_t, 2> detectors{
        Index(2, 1),
        Index(1, 2),
    };
    TensorlessFpmSandboxNode source_node{};
    source_node.energy_subunits = kShots;
    source_node.momentum[0][0] =
        static_cast<int64_t>(requested[0]);
    source_node.momentum[1][1] =
        static_cast<int64_t>(requested[1]);
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), source, &source_node),
            "Tensorless_FpmSandboxSetNode(Clifford+T source)"))
        return false;

    std::array<uint64_t, 2> initial{};
    for (uint32_t detector = 0; detector < 2; ++detector) {
        if (capacities[detector] >
            tensorless_adapter::kEnergyCeiling)
            return false;
        TensorlessFpmSandboxNode detector_node{};
        detector_node.energy_subunits =
            tensorless_adapter::kEnergyCeiling -
            capacities[detector];
        initial[detector] = detector_node.energy_subunits;
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(), detectors[detector],
                    &detector_node),
                "Tensorless_FpmSandboxSetNode(Clifford+T detector)"))
            return false;
    }

    const uint64_t node_count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(
        static_cast<size_t>(node_count), 0);
    std::vector<uint64_t> payloads(
        static_cast<size_t>(node_count), 0);
    payloads[source] = kShots;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                node_count, &stats),
            "Tensorless_FpmSandboxStep(Clifford+T detectors)"))
        return false;

    result = {};
    for (uint32_t detector = 0; detector < 2; ++detector) {
        TensorlessFpmSandboxNode node{};
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxGetNode(
                    sandbox.get(), detectors[detector], &node),
                "Tensorless_FpmSandboxGetNode(Clifford+T detector)"))
            return false;
        result.accepted[detector] =
            node.energy_subunits - initial[detector];
    }
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];
    return tensorless_adapter::ResidualsAreZero(stats);
}

struct RunResult {
    ExactProbability z_zero{};
    ExactProbability z_one{};
    ExactProbability x_zero{};
    ExactProbability x_one{};
    uint64_t x_threshold_q32 = 0;
    std::array<uint64_t, 2> z_counts{};
    std::array<uint64_t, 2> x_counts{};
    RoutingResult routed{};
    RoutingResult saturation{};

    bool operator==(const RunResult&) const = default;
};

bool CheckGateOracles() {
    CliffordTState seed(2);
    seed.ApplyH(0);
    seed.ApplyCnot(0, 1);
    seed.ApplyH(1);

    CliffordTState t_squared = seed;
    t_squared.ApplyT(0);
    t_squared.ApplyT(0);
    CliffordTState s = seed;
    s.ApplyS(0);
    if (!(t_squared == s))
        return false;

    CliffordTState t_eighth = seed;
    for (uint32_t count = 0; count < 8; ++count)
        t_eighth.ApplyT(1);
    if (!(t_eighth == seed))
        return false;

    CliffordTState bell(2);
    bell.ApplyH(0);
    bell.ApplyCnot(0, 1);
    return bell.OutcomeProbability(0) ==
               std::optional<ExactProbability>{{1, 0, 1}} &&
           bell.OutcomeProbability(1) ==
               std::optional<ExactProbability>{{0, 0, 1}} &&
           bell.OutcomeProbability(2) ==
               std::optional<ExactProbability>{{0, 0, 1}} &&
           bell.OutcomeProbability(3) ==
               std::optional<ExactProbability>{{1, 0, 1}};
}

bool Execute(RunResult& result) {
    if (!CheckGateOracles())
        return false;

    CliffordTState magic(1);
    magic.ApplyH(0);
    magic.ApplyT(0);
    const std::optional<ExactProbability> z_zero =
        magic.OutcomeProbability(0);
    const std::optional<ExactProbability> z_one =
        magic.OutcomeProbability(1);
    if (!z_zero.has_value() || !z_one.has_value() ||
        *z_zero != ExactProbability{1, 0, 1} ||
        *z_one != ExactProbability{1, 0, 1})
        return false;

    CliffordTState x_basis = magic;
    x_basis.ApplyH(0);
    const std::optional<ExactProbability> x_zero =
        x_basis.OutcomeProbability(0);
    const std::optional<ExactProbability> x_one =
        x_basis.OutcomeProbability(1);
    if (!x_zero.has_value() || !x_one.has_value() ||
        *x_zero != ExactProbability{2, 1, 2} ||
        *x_one != ExactProbability{2, -1, 2})
        return false;

    const std::optional<uint64_t> x_threshold =
        ProbabilityToQ32(*x_zero);
    if (!x_threshold.has_value() ||
        *x_threshold != UINT64_C(3665983897))
        return false;

    result = {};
    result.z_zero = *z_zero;
    result.z_one = *z_one;
    result.x_zero = *x_zero;
    result.x_one = *x_one;
    result.x_threshold_q32 = *x_threshold;
    result.z_counts = SampleBinary(kQ32Scale / 2, kShots, kSeed);
    result.x_counts = SampleBinary(*x_threshold, kShots, kSeed);

    if (result.z_counts[0] + result.z_counts[1] != kShots ||
        result.x_counts[0] + result.x_counts[1] != kShots ||
        result.x_counts[0] <= result.x_counts[1])
        return false;
    if (!RouteCounts(
            result.x_counts, {kShots, kShots}, result.routed) ||
        !RouteCounts(
            result.x_counts, {2000, 1000}, result.saturation))
        return false;
    return result.routed.accepted == result.x_counts &&
           result.routed.external_exhaust == 0 &&
           result.saturation.accepted ==
               std::array<uint64_t, 2>{2000, 1000} &&
           result.saturation.external_exhaust == 7000;
}

void WriteProbability(const ExactProbability& probability) {
    std::cout
        << "{\"integer\":" << probability.integer
        << ",\"sqrt2_coefficient\":"
        << probability.sqrt2_coefficient
        << ",\"denominator_power2\":"
        << probability.denominator_power << "}";
}

void WriteTrace(const RunResult& result) {
    std::cout
        << "{\"type\":\"exact_oracle\","
        << "\"name\":\"t_plus_z_basis\","
        << "\"circuit\":\"H,T\","
        << "\"probability_0\":";
    WriteProbability(result.z_zero);
    std::cout << ",\"probability_1\":";
    WriteProbability(result.z_one);
    std::cout
        << ",\"expected\":\"1/2,1/2\"}\n"
        << "{\"type\":\"exact_oracle\","
        << "\"name\":\"t_plus_x_basis\","
        << "\"circuit\":\"H,T,H\","
        << "\"probability_0\":";
    WriteProbability(result.x_zero);
    std::cout << ",\"probability_1\":";
    WriteProbability(result.x_one);
    std::cout
        << ",\"expected\":\"(2+sqrt(2))/4,"
        << "(2-sqrt(2))/4\"}\n"
        << "{\"type\":\"counterexample\","
        << "\"name\":\"probability_only_injection\","
        << "\"coherent_x_probability\":";
    WriteProbability(result.x_zero);
    std::cout
        << ",\"incoherent_mixture_x_probability\":";
    WriteProbability({1, 0, 1});
    std::cout
        << ",\"phase_required\":true}\n"
        << "{\"type\":\"sampling\","
        << "\"seed\":" << kSeed << ","
        << "\"fixed_point_bits\":32,"
        << "\"sqrt2_q32\":" << kSqrt2Q32 << ","
        << "\"threshold_0_q32\":"
        << result.x_threshold_q32 << ","
        << "\"probability_error_bound\":\"less_than_2^-31\","
        << "\"z_counts\":["
        << result.z_counts[0] << "," << result.z_counts[1] << "],"
        << "\"x_counts\":["
        << result.x_counts[0] << "," << result.x_counts[1] << "]}\n"
        << "{\"type\":\"routing\","
        << "\"requested\":["
        << result.x_counts[0] << "," << result.x_counts[1] << "],"
        << "\"accepted\":["
        << result.routed.accepted[0] << ","
        << result.routed.accepted[1] << "],"
        << "\"external_exhaust\":"
        << result.routed.external_exhaust << ","
        << "\"energy_residual\":"
        << result.routed.energy_residual << "}\n"
        << "{\"type\":\"safety_case\","
        << "\"name\":\"detector_saturation\","
        << "\"capacities\":[2000,1000],"
        << "\"accepted\":["
        << result.saturation.accepted[0] << ","
        << result.saturation.accepted[1] << "],"
        << "\"external_exhaust\":"
        << result.saturation.external_exhaust << ","
        << "\"energy_residual\":"
        << result.saturation.energy_residual << "}\n"
        << "{\"type\":\"summary\","
        << "\"adapter\":\"clifford_t_validator\","
        << "\"gate_set\":\"H,S,T,X,Z,CNOT\","
        << "\"max_qubits\":" << kMaxQubits << ","
        << "\"state_storage\":\"exponential\","
        << "\"floating_point_used\":false,"
        << "\"magic_state_distillation\":false,"
        << "\"quantum_hardware_replacement\":false,"
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
