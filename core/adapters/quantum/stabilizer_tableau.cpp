#include "../common/sandbox_adapter.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr uint64_t kShots = 10000;
constexpr uint64_t kSeed = UINT64_C(0x5A17B1E5C0FFEE11);
constexpr uint32_t kScaleQubits = 64;
constexpr uint64_t kScaleShots = 2048;

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

    bool NextBit() {
        return (Next() & 1U) != 0;
    }

private:
    uint64_t state_;
};

struct PauliRow {
    std::vector<uint8_t> x;
    std::vector<uint8_t> z;
    uint8_t phase = 0;

    bool operator==(const PauliRow&) const = default;
};

class StabilizerTableau {
public:
    explicit StabilizerTableau(uint32_t qubits)
        : qubits_(qubits),
          rows_(static_cast<size_t>(2U * qubits + 1U)) {
        for (PauliRow& row : rows_) {
            row.x.resize(qubits);
            row.z.resize(qubits);
        }
        for (uint32_t qubit = 0; qubit < qubits; ++qubit) {
            rows_[qubit].x[qubit] = 1;
            rows_[qubits + qubit].z[qubit] = 1;
        }
    }

    uint32_t qubits() const {
        return qubits_;
    }

    void ApplyH(uint32_t qubit) {
        for (uint32_t row = 0; row < 2U * qubits_; ++row) {
            PauliRow& current = rows_[row];
            current.phase ^= current.x[qubit] & current.z[qubit];
            std::swap(current.x[qubit], current.z[qubit]);
        }
    }

    void ApplyS(uint32_t qubit) {
        for (uint32_t row = 0; row < 2U * qubits_; ++row) {
            PauliRow& current = rows_[row];
            current.phase ^= current.x[qubit] & current.z[qubit];
            current.z[qubit] ^= current.x[qubit];
        }
    }

    void ApplyX(uint32_t qubit) {
        for (uint32_t row = 0; row < 2U * qubits_; ++row)
            rows_[row].phase ^= rows_[row].z[qubit];
    }

    void ApplyZ(uint32_t qubit) {
        for (uint32_t row = 0; row < 2U * qubits_; ++row)
            rows_[row].phase ^= rows_[row].x[qubit];
    }

    void ApplyCnot(uint32_t control, uint32_t target) {
        for (uint32_t row = 0; row < 2U * qubits_; ++row) {
            PauliRow& current = rows_[row];
            current.phase ^=
                current.x[control] & current.z[target] &
                (current.x[target] ^ current.z[control] ^ 1U);
            current.x[target] ^= current.x[control];
            current.z[control] ^= current.z[target];
        }
    }

    std::optional<bool> MeasureZ(
        uint32_t qubit,
        SplitMix64& random) {
        uint32_t pivot = 2U * qubits_;
        for (uint32_t row = qubits_; row < 2U * qubits_; ++row) {
            if (rows_[row].x[qubit] != 0) {
                pivot = row;
                break;
            }
        }

        if (pivot != 2U * qubits_) {
            for (uint32_t row = 0; row < 2U * qubits_; ++row) {
                if (row != pivot && rows_[row].x[qubit] != 0 &&
                    !RowMultiply(row, pivot))
                    return std::nullopt;
            }
            rows_[pivot - qubits_] = rows_[pivot];
            PauliRow replacement{};
            replacement.x.resize(qubits_);
            replacement.z.resize(qubits_);
            replacement.z[qubit] = 1;
            replacement.phase = random.NextBit() ? 1 : 0;
            rows_[pivot] = std::move(replacement);
            return rows_[pivot].phase != 0;
        }

        PauliRow& scratch = rows_[2U * qubits_];
        scratch = {};
        scratch.x.resize(qubits_);
        scratch.z.resize(qubits_);
        for (uint32_t row = 0; row < qubits_; ++row) {
            if (rows_[row].x[qubit] != 0 &&
                !RowMultiply(2U * qubits_, qubits_ + row))
                return std::nullopt;
        }
        return scratch.phase != 0;
    }

    bool operator==(const StabilizerTableau&) const = default;

private:
    static int PhaseContribution(
        uint8_t left_x,
        uint8_t left_z,
        uint8_t right_x,
        uint8_t right_z) {
        if (left_x == 0 && left_z == 0)
            return 0;
        if (left_x != 0 && left_z != 0)
            return static_cast<int>(right_z) -
                   static_cast<int>(right_x);
        if (left_x != 0)
            return right_z != 0
                       ? 2 * static_cast<int>(right_x) - 1
                       : 0;
        return right_x != 0
                   ? 1 - 2 * static_cast<int>(right_z)
                   : 0;
    }

    bool RowMultiply(uint32_t destination, uint32_t source) {
        PauliRow& target = rows_[destination];
        const PauliRow& factor = rows_[source];
        int phase_sum =
            2 * static_cast<int>(target.phase) +
            2 * static_cast<int>(factor.phase);
        for (uint32_t qubit = 0; qubit < qubits_; ++qubit)
            phase_sum += PhaseContribution(
                factor.x[qubit], factor.z[qubit],
                target.x[qubit], target.z[qubit]);
        const int normalized = (phase_sum % 4 + 4) % 4;
        if (normalized != 0 && normalized != 2)
            return false;
        target.phase = static_cast<uint8_t>(normalized / 2);
        for (uint32_t qubit = 0; qubit < qubits_; ++qubit) {
            target.x[qubit] ^= factor.x[qubit];
            target.z[qubit] ^= factor.z[qubit];
        }
        return true;
    }

    uint32_t qubits_;
    std::vector<PauliRow> rows_;
};

using Histogram = std::map<std::string, uint64_t>;

std::optional<std::string> MeasureAll(
    StabilizerTableau& tableau,
    SplitMix64& random) {
    std::string bits;
    bits.reserve(tableau.qubits());
    for (uint32_t qubit = 0; qubit < tableau.qubits(); ++qubit) {
        const std::optional<bool> outcome =
            tableau.MeasureZ(qubit, random);
        if (!outcome.has_value())
            return std::nullopt;
        bits.push_back(*outcome ? '1' : '0');
    }
    return bits;
}

std::optional<Histogram> Sample(
    const StabilizerTableau& master,
    uint64_t shots,
    uint64_t seed) {
    SplitMix64 random(seed);
    Histogram histogram;
    for (uint64_t shot = 0; shot < shots; ++shot) {
        StabilizerTableau scratch = master;
        const std::optional<std::string> outcome =
            MeasureAll(scratch, random);
        if (!outcome.has_value())
            return std::nullopt;
        ++histogram[*outcome];
    }
    return histogram;
}

uint64_t Count(const Histogram& histogram, std::string_view outcome) {
    const auto found = histogram.find(std::string(outcome));
    return found == histogram.end() ? 0 : found->second;
}

uint64_t Total(const Histogram& histogram) {
    uint64_t total = 0;
    for (const auto& [outcome, count] : histogram) {
        (void)outcome;
        total += count;
    }
    return total;
}

struct RoutingResult {
    std::array<uint64_t, 4> accepted{};
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};

    bool operator==(const RoutingResult&) const = default;
};

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(4, 1, x, y, 0);
}

bool RouteDetectorCounts(
    const std::array<uint64_t, 4>& requested,
    const std::array<uint64_t, 4>& capacities,
    RoutingResult& result) {
    uint64_t requested_total = 0;
    for (uint32_t outcome = 0; outcome < 4; ++outcome) {
        requested_total += requested[outcome];
        if (capacities[outcome] >
            tensorless_adapter::kEnergyCeiling)
            return false;
    }
    if (requested_total != kShots)
        return false;

    TensorlessFpmSandboxCreateInfo info =
        tensorless_adapter::IsotropicConfig(2, 4, 1);
    info.isotropic_face_weight = 0;
    tensorless_adapter::Sandbox sandbox;
    if (!sandbox.Create(info))
        return false;

    const uint64_t count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(static_cast<size_t>(count), 0);
    std::vector<uint64_t> payloads(static_cast<size_t>(count), 0);
    std::array<uint64_t, 4> detector_initial{};
    for (uint32_t outcome = 0; outcome < 4; ++outcome) {
        const uint64_t source = Index(0, outcome);
        const uint64_t detector = Index(1, outcome);
        TensorlessFpmSandboxNode source_node{};
        source_node.energy_subunits = requested[outcome];
        source_node.momentum[0][0] =
            static_cast<int64_t>(requested[outcome]);
        TensorlessFpmSandboxNode detector_node{};
        detector_node.energy_subunits =
            tensorless_adapter::kEnergyCeiling -
            capacities[outcome];
        detector_initial[outcome] =
            detector_node.energy_subunits;
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(), source, &source_node),
                "Tensorless_FpmSandboxSetNode(outcome source)") ||
            !tensorless_adapter::Check(
                Tensorless_FpmSandboxSetNode(
                    sandbox.get(), detector, &detector_node),
                "Tensorless_FpmSandboxSetNode(outcome detector)"))
            return false;
        payloads[source] = requested[outcome];
    }

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                count, &stats),
            "Tensorless_FpmSandboxStep(outcome detectors)"))
        return false;

    result = {};
    for (uint32_t outcome = 0; outcome < 4; ++outcome) {
        TensorlessFpmSandboxNode detector_node{};
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxGetNode(
                    sandbox.get(), Index(1, outcome),
                    &detector_node),
                "Tensorless_FpmSandboxGetNode(outcome detector)"))
            return false;
        result.accepted[outcome] =
            detector_node.energy_subunits -
            detector_initial[outcome];
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
    Histogram bell;
    Histogram inverse_bell;
    Histogram phase_bell;
    Histogram product;
    Histogram ghz;
    RoutingResult routed;
    RoutingResult saturation;

    bool operator==(const RunResult&) const = default;
};

bool CheckGateIdentities() {
    StabilizerTableau initial(2);

    StabilizerTableau hh = initial;
    hh.ApplyH(0);
    hh.ApplyH(0);
    if (!(hh == initial))
        return false;

    StabilizerTableau ssss = initial;
    for (uint32_t count = 0; count < 4; ++count)
        ssss.ApplyS(1);
    if (!(ssss == initial))
        return false;

    StabilizerTableau cnot_cnot = initial;
    cnot_cnot.ApplyCnot(0, 1);
    cnot_cnot.ApplyCnot(0, 1);
    if (!(cnot_cnot == initial))
        return false;

    StabilizerTableau xz = initial;
    xz.ApplyX(0);
    xz.ApplyZ(1);
    xz.ApplyX(0);
    xz.ApplyZ(1);
    return xz == initial;
}

bool Execute(RunResult& result) {
    if (!CheckGateIdentities()) {
        std::cerr << "gate identity check failed\n";
        return false;
    }

    StabilizerTableau bell(2);
    bell.ApplyH(0);
    bell.ApplyCnot(0, 1);
    const std::optional<Histogram> bell_sample =
        Sample(bell, kShots, kSeed);
    const std::optional<Histogram> bell_replay =
        Sample(bell, kShots, kSeed);
    if (!bell_sample.has_value() ||
        !bell_replay.has_value() ||
        *bell_sample != *bell_replay ||
        Total(*bell_sample) != kShots ||
        bell_sample->size() != 2 ||
        Count(*bell_sample, "00") <= 2000 ||
        Count(*bell_sample, "11") <= 2000 ||
        Count(*bell_sample, "01") != 0 ||
        Count(*bell_sample, "10") != 0) {
        std::cerr << "Bell sample check failed\n";
        return false;
    }

    StabilizerTableau inverse_bell = bell;
    inverse_bell.ApplyCnot(0, 1);
    inverse_bell.ApplyH(0);
    const std::optional<Histogram> inverse_sample =
        Sample(inverse_bell, kShots, kSeed);
    if (!inverse_sample.has_value() ||
        inverse_sample->size() != 1 ||
        Count(*inverse_sample, "00") != kShots) {
        std::cerr << "inverse Bell check failed\n";
        return false;
    }

    StabilizerTableau phase_bell = bell;
    phase_bell.ApplyZ(0);
    phase_bell.ApplyCnot(0, 1);
    phase_bell.ApplyH(0);
    const std::optional<Histogram> phase_sample =
        Sample(phase_bell, kShots, kSeed);
    if (!phase_sample.has_value() ||
        phase_sample->size() != 1 ||
        Count(*phase_sample, "10") != kShots) {
        std::cerr << "phase Bell check failed\n";
        return false;
    }

    StabilizerTableau product(2);
    product.ApplyX(0);
    const std::optional<Histogram> product_sample =
        Sample(product, kShots, kSeed);
    if (!product_sample.has_value() ||
        product_sample->size() != 1 ||
        Count(*product_sample, "10") != kShots) {
        std::cerr << "product-state check failed\n";
        return false;
    }

    StabilizerTableau ghz(kScaleQubits);
    ghz.ApplyH(0);
    for (uint32_t qubit = 1; qubit < kScaleQubits; ++qubit)
        ghz.ApplyCnot(0, qubit);
    const std::optional<Histogram> ghz_sample =
        Sample(ghz, kScaleShots, kSeed);
    const std::string all_zero(kScaleQubits, '0');
    const std::string all_one(kScaleQubits, '1');
    if (!ghz_sample.has_value() ||
        Total(*ghz_sample) != kScaleShots ||
        ghz_sample->size() != 2 ||
        Count(*ghz_sample, all_zero) == 0 ||
        Count(*ghz_sample, all_one) == 0) {
        std::cerr << "GHZ scale check failed\n";
        return false;
    }

    const std::array<uint64_t, 4> detector_counts{
        Count(*bell_sample, "00"),
        Count(*bell_sample, "01"),
        Count(*bell_sample, "10"),
        Count(*bell_sample, "11"),
    };
    result = {
        *bell_sample,
        *inverse_sample,
        *phase_sample,
        *product_sample,
        *ghz_sample,
        {},
        {},
    };
    if (!RouteDetectorCounts(
            detector_counts,
            {kShots, kShots, kShots, kShots},
            result.routed) ||
        !RouteDetectorCounts(
            detector_counts,
            {2000, 2000, 2000, 2000},
            result.saturation)) {
        std::cerr << "detector routing failed\n";
        return false;
    }

    const bool routing_matches =
           result.routed.accepted == detector_counts &&
           result.routed.external_exhaust == 0 &&
           result.saturation.accepted ==
               std::array<uint64_t, 4>{2000, 0, 0, 2000} &&
           result.saturation.external_exhaust == 6000;
    if (!routing_matches) {
        std::cerr
            << "detector routing oracle failed: requested ["
            << detector_counts[0] << "," << detector_counts[1]
            << "," << detector_counts[2] << ","
            << detector_counts[3] << "], routed ["
            << result.routed.accepted[0] << ","
            << result.routed.accepted[1] << ","
            << result.routed.accepted[2] << ","
            << result.routed.accepted[3] << "], exhaust "
            << result.routed.external_exhaust << ", saturated ["
            << result.saturation.accepted[0] << ","
            << result.saturation.accepted[1] << ","
            << result.saturation.accepted[2] << ","
            << result.saturation.accepted[3] << "], exhaust "
            << result.saturation.external_exhaust << "\n";
    }
    return routing_matches;
}

void WriteCounts(const Histogram& histogram) {
    std::cout
        << "[" << Count(histogram, "00")
        << "," << Count(histogram, "01")
        << "," << Count(histogram, "10")
        << "," << Count(histogram, "11") << "]";
}

void WriteRouting(const RoutingResult& routing) {
    std::cout
        << "\"accepted\":["
        << routing.accepted[0] << ","
        << routing.accepted[1] << ","
        << routing.accepted[2] << ","
        << routing.accepted[3] << "],"
        << "\"external_exhaust\":"
        << routing.external_exhaust << ","
        << "\"energy_residual\":"
        << routing.energy_residual;
}

void WriteTrace(const RunResult& result) {
    std::cout
        << "{\"type\":\"sample\","
        << "\"name\":\"bell\","
        << "\"qubits\":2,"
        << "\"gates\":\"H(0),CNOT(0,1)\","
        << "\"shots\":" << kShots << ","
        << "\"seed\":" << kSeed << ","
        << "\"bit_order\":\"q0_most_significant\","
        << "\"counts\":";
    WriteCounts(result.bell);
    std::cout
        << ",\"support_verified\":true,"
        << "\"replay_verified\":true}\n"
        << "{\"type\":\"sample\","
        << "\"name\":\"inverse_bell\","
        << "\"qubits\":2,"
        << "\"gates\":\"H(0),CNOT(0,1),CNOT(0,1),H(0)\","
        << "\"shots\":" << kShots << ","
        << "\"counts\":";
    WriteCounts(result.inverse_bell);
    std::cout
        << ",\"expected\":\"00\"}\n"
        << "{\"type\":\"sample\","
        << "\"name\":\"phase_bell\","
        << "\"qubits\":2,"
        << "\"gates\":\"H(0),CNOT(0,1),Z(0),CNOT(0,1),H(0)\","
        << "\"shots\":" << kShots << ","
        << "\"counts\":";
    WriteCounts(result.phase_bell);
    std::cout
        << ",\"expected\":\"10\","
        << "\"phase_oracle_verified\":true}\n"
        << "{\"type\":\"sample\","
        << "\"name\":\"product_x_q0\","
        << "\"qubits\":2,"
        << "\"gates\":\"X(0)\","
        << "\"shots\":" << kShots << ","
        << "\"counts\":";
    WriteCounts(result.product);
    std::cout
        << ",\"expected\":\"10\","
        << "\"bit_order\":\"q0_most_significant\"}\n"
        << "{\"type\":\"scale_case\","
        << "\"name\":\"ghz_64\","
        << "\"qubits\":" << kScaleQubits << ","
        << "\"shots\":" << kScaleShots << ","
        << "\"observed_outcomes\":"
        << result.ghz.size() << ","
        << "\"all_zero\":"
        << Count(result.ghz, std::string(kScaleQubits, '0')) << ","
        << "\"all_one\":"
        << Count(result.ghz, std::string(kScaleQubits, '1')) << ","
        << "\"state_vector_entries_materialized\":0}\n"
        << "{\"type\":\"routing\","
        << "\"name\":\"bell_detector_counts\","
        << "\"requested\":";
    WriteCounts(result.bell);
    std::cout << ",";
    WriteRouting(result.routed);
    std::cout
        << "}\n"
        << "{\"type\":\"safety_case\","
        << "\"name\":\"detector_saturation\","
        << "\"requested\":";
    WriteCounts(result.bell);
    std::cout
        << ",\"capacities\":[2000,2000,2000,2000],";
    WriteRouting(result.saturation);
    std::cout
        << ",\"detector_saturation\":true}\n"
        << "{\"type\":\"summary\","
        << "\"adapter\":\"stabilizer_tableau_validator\","
        << "\"gate_set\":\"H,S,X,Z,CNOT\","
        << "\"tableau_phase_tracking\":true,"
        << "\"fresh_tableau_per_shot\":true,"
        << "\"full_distribution_enumerated\":false,"
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
