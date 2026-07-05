#include "../common/sandbox_adapter.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr uint64_t kQ32Scale = UINT64_C(1) << 32U;
constexpr uint64_t kIdealZeroQ32 = UINT64_C(3665983897);
constexpr uint64_t kMaximumShots = 1000000;

struct HardwareResult {
    std::string provider;
    std::string backend;
    std::string job_id;
    std::string circuit;
    std::string logical_circuit_sha256;
    std::string transpiled_circuit_sha256;
    std::string submitted_at_utc;
    std::string completed_at_utc;
    std::string calibration_timestamp_utc;
    std::string isa_gate_counts_json;
    std::string qiskit_version;
    std::string qiskit_ibm_runtime_version;
    uint64_t identity_cycles = 0;
    uint64_t shots = 0;
    std::array<uint64_t, 2> counts{};
    uint64_t optimization_level = 0;
    bool synthetic = false;
    bool dynamical_decoupling_enabled = false;
    bool twirling_enabled = false;
};

struct Metrics {
    std::array<uint64_t, 2> ideal_counts{};
    std::array<int64_t, 2> signed_residuals{};
    uint64_t l1_count_distance = 0;
    uint64_t total_variation_q32 = 0;
    uint64_t hellinger_q32 = 0;
    uint64_t hellinger_squared_q32 = 0;
};

struct RoutingResult {
    std::array<uint64_t, 2> accepted{};
    uint64_t external_exhaust = 0;
    int64_t energy_residual = 0;
    std::array<std::array<int64_t, 3>, 3> momentum_residual{};
};

std::optional<std::string> ReadFile(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::nullopt;
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof())
        return std::nullopt;
    return buffer.str();
}

std::optional<size_t> ValueStart(
    std::string_view document,
    std::string_view key) {
    const std::string marker = "\"" + std::string(key) + "\"";
    size_t search = 0;
    while (true) {
        const size_t key_position = document.find(marker, search);
        if (key_position == std::string_view::npos)
            return std::nullopt;
        size_t colon = key_position + marker.size();
        while (colon < document.size() &&
               std::isspace(
                   static_cast<unsigned char>(document[colon])) != 0)
            ++colon;
        if (colon < document.size() && document[colon] == ':') {
            size_t value = colon + 1;
            while (value < document.size() &&
                   std::isspace(
                       static_cast<unsigned char>(document[value])) != 0)
                ++value;
            return value;
        }
        search = key_position + marker.size();
    }
}

std::optional<std::string> ExtractString(
    std::string_view document,
    std::string_view key) {
    const std::optional<size_t> start = ValueStart(document, key);
    if (!start.has_value() || *start >= document.size() ||
        document[*start] != '"')
        return std::nullopt;
    const size_t end = document.find('"', *start + 1);
    if (end == std::string_view::npos)
        return std::nullopt;
    const std::string value(document.substr(*start + 1, end - *start - 1));
    if (value.find('\\') != std::string::npos)
        return std::nullopt;
    return value;
}

std::optional<uint64_t> ExtractUint(
    std::string_view document,
    std::string_view key) {
    const std::optional<size_t> start = ValueStart(document, key);
    if (!start.has_value() || *start >= document.size() ||
        !std::isdigit(static_cast<unsigned char>(document[*start])))
        return std::nullopt;
    uint64_t value = 0;
    size_t cursor = *start;
    while (cursor < document.size() &&
           std::isdigit(static_cast<unsigned char>(document[cursor])) != 0) {
        const uint64_t digit =
            static_cast<uint64_t>(document[cursor] - '0');
        if (value >
            (std::numeric_limits<uint64_t>::max() - digit) / 10)
            return std::nullopt;
        value = value * 10 + digit;
        ++cursor;
    }
    return value;
}

std::optional<bool> ExtractBool(
    std::string_view document,
    std::string_view key) {
    const std::optional<size_t> start = ValueStart(document, key);
    if (!start.has_value())
        return std::nullopt;
    if (document.substr(*start, 4) == "true")
        return true;
    if (document.substr(*start, 5) == "false")
        return false;
    return std::nullopt;
}

std::optional<std::string_view> ExtractObject(
    std::string_view document,
    std::string_view key) {
    const std::optional<size_t> start = ValueStart(document, key);
    if (!start.has_value() || *start >= document.size() ||
        document[*start] != '{')
        return std::nullopt;
    uint32_t depth = 0;
    bool in_string = false;
    for (size_t cursor = *start; cursor < document.size(); ++cursor) {
        const char current = document[cursor];
        if (current == '"' &&
            (cursor == 0 || document[cursor - 1] != '\\'))
            in_string = !in_string;
        if (in_string)
            continue;
        if (current == '{')
            ++depth;
        else if (current == '}' && --depth == 0)
            return document.substr(*start, cursor - *start + 1);
    }
    return std::nullopt;
}

std::string CompactJson(std::string_view value) {
    std::string compact;
    compact.reserve(value.size());
    bool in_string = false;
    for (size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '"' &&
            (index == 0 || value[index - 1] != '\\'))
            in_string = !in_string;
        if (!in_string &&
            std::isspace(static_cast<unsigned char>(character)) != 0)
            continue;
        compact.push_back(character);
    }
    return compact;
}

bool ParseHardwareResult(
    std::string_view document,
    HardwareResult& result) {
    const std::optional<uint64_t> schema =
        ExtractUint(document, "schema_version");
    const std::optional<std::string> provider =
        ExtractString(document, "provider");
    const std::optional<std::string> backend =
        ExtractString(document, "backend");
    const std::optional<std::string> job_id =
        ExtractString(document, "job_id");
    const std::optional<std::string> circuit =
        ExtractString(document, "circuit");
    const std::optional<std::string> logical_hash =
        ExtractString(document, "logical_circuit_sha256");
    const std::optional<std::string> transpiled_hash =
        ExtractString(document, "transpiled_circuit_sha256");
    const std::optional<std::string> completed =
        ExtractString(document, "completed_at_utc");
    const std::optional<std::string> submitted =
        ExtractString(document, "submitted_at_utc");
    std::optional<std::string> calibration =
        ExtractString(document, "calibration_timestamp_utc");
    const std::optional<std::string_view> isa_gate_counts =
        ExtractObject(document, "isa_gate_counts");
    const std::optional<std::string> qiskit_version =
        ExtractString(document, "qiskit_version");
    const std::optional<std::string> runtime_version =
        ExtractString(document, "qiskit_ibm_runtime_version");
    const std::optional<uint64_t> shots =
        ExtractUint(document, "shots");
    const std::optional<uint64_t> optimization =
        ExtractUint(document, "optimization_level");
    const std::optional<bool> synthetic =
        ExtractBool(document, "synthetic");
    const std::optional<bool> dynamical_decoupling =
        ExtractBool(document, "dynamical_decoupling_enabled");
    const std::optional<bool> twirling =
        ExtractBool(document, "twirling_enabled");
    const std::optional<std::string_view> counts =
        ExtractObject(document, "counts");
    if (!schema.has_value() || *schema != 1) {
        std::cerr << "invalid schema_version\n";
        return false;
    }
    if (!provider.has_value() || *provider != "ibm_quantum") {
        std::cerr << "invalid provider\n";
        return false;
    }
    if (!backend.has_value() || !job_id.has_value() ||
        !circuit.has_value() ||
        (*circuit != "h_t_h_measure" &&
         *circuit != "h_t_h_sx4_identity")) {
        std::cerr << "invalid backend, job_id, or circuit\n";
        return false;
    }
    if (!logical_hash.has_value() || logical_hash->size() != 64 ||
        !transpiled_hash.has_value() || transpiled_hash->size() != 64) {
        std::cerr << "invalid circuit hash\n";
        return false;
    }
    if (!submitted.has_value() || !completed.has_value() ||
        !isa_gate_counts.has_value() ||
        !qiskit_version.has_value() || !runtime_version.has_value() ||
        !shots.has_value() ||
        *shots == 0 || *shots > kMaximumShots) {
        std::cerr << "invalid provenance timestamp, gate counts, or shots\n";
        return false;
    }
    if (!calibration.has_value()) {
        const std::optional<size_t> calibration_start =
            ValueStart(document, "calibration_timestamp_utc");
        if (!calibration_start.has_value() ||
            document.substr(*calibration_start, 4) != "null") {
            std::cerr << "invalid calibration timestamp\n";
            return false;
        }
        calibration = "";
    }
    if (!optimization.has_value() || *optimization > 3 ||
        !synthetic.has_value() ||
        !dynamical_decoupling.has_value() ||
        !twirling.has_value() || !counts.has_value()) {
        std::cerr
            << "invalid execution options or counts object: optimization="
            << optimization.has_value()
            << ", synthetic=" << synthetic.has_value()
            << ", dynamical_decoupling="
            << dynamical_decoupling.has_value()
            << ", twirling=" << twirling.has_value()
            << ", counts=" << counts.has_value() << "\n";
        return false;
    }
    const std::optional<uint64_t> zero = ExtractUint(*counts, "0");
    const std::optional<uint64_t> one = ExtractUint(*counts, "1");
    uint64_t identity_cycles = 0;
    if (*circuit == "h_t_h_sx4_identity") {
        const std::optional<uint64_t> parsed_cycles =
            ExtractUint(document, "identity_cycles");
        if (!parsed_cycles.has_value()) {
            std::cerr << "missing identity_cycles\n";
            return false;
        }
        identity_cycles = *parsed_cycles;
    }
    if (!zero.has_value() || !one.has_value() ||
        *zero + *one != *shots) {
        std::cerr << "invalid outcome counts\n";
        return false;
    }
    result = {
        *provider,
        *backend,
        *job_id,
        *circuit,
        *logical_hash,
        *transpiled_hash,
        *submitted,
        *completed,
        *calibration,
        CompactJson(*isa_gate_counts),
        *qiskit_version,
        *runtime_version,
        identity_cycles,
        *shots,
        {*zero, *one},
        *optimization,
        *synthetic,
        *dynamical_decoupling,
        *twirling,
    };
    return true;
}

uint64_t IntegerSquareRoot(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = UINT64_C(1) << 62U;
    while (bit > value)
        bit >>= 2U;
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

Metrics ComputeMetrics(const HardwareResult& result) {
    Metrics metrics{};
    metrics.ideal_counts[0] =
        (kIdealZeroQ32 * result.shots + kQ32Scale / 2) /
        kQ32Scale;
    metrics.ideal_counts[1] =
        result.shots - metrics.ideal_counts[0];
    for (uint32_t outcome = 0; outcome < 2; ++outcome) {
        metrics.signed_residuals[outcome] =
            static_cast<int64_t>(result.counts[outcome]) -
            static_cast<int64_t>(metrics.ideal_counts[outcome]);
        metrics.l1_count_distance += static_cast<uint64_t>(
            metrics.signed_residuals[outcome] < 0
                ? -metrics.signed_residuals[outcome]
                : metrics.signed_residuals[outcome]);
    }

    const uint64_t observed_zero_q32 =
        (result.counts[0] * kQ32Scale + result.shots / 2) /
        result.shots;
    metrics.total_variation_q32 =
        observed_zero_q32 > kIdealZeroQ32
            ? observed_zero_q32 - kIdealZeroQ32
            : kIdealZeroQ32 - observed_zero_q32;
    const uint64_t observed_one_q32 =
        kQ32Scale - observed_zero_q32;
    const uint64_t ideal_one_q32 =
        kQ32Scale - kIdealZeroQ32;
    const uint64_t coefficient_q32 =
        IntegerSquareRoot(observed_zero_q32 * kIdealZeroQ32) +
        IntegerSquareRoot(observed_one_q32 * ideal_one_q32);
    metrics.hellinger_squared_q32 =
        coefficient_q32 >= kQ32Scale
            ? 0
            : kQ32Scale - coefficient_q32;
    metrics.hellinger_q32 =
        metrics.hellinger_squared_q32 == kQ32Scale
            ? kQ32Scale
            : IntegerSquareRoot(
                  metrics.hellinger_squared_q32 * kQ32Scale);
    return metrics;
}

uint64_t Index(uint32_t x, uint32_t y) {
    return tensorless_adapter::Flat(3, 1, x, y, 0);
}

bool RouteCounts(
    const HardwareResult& hardware,
    RoutingResult& result) {
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
    source_node.energy_subunits = hardware.shots;
    source_node.momentum[0][0] =
        static_cast<int64_t>(hardware.counts[0]);
    source_node.momentum[1][1] =
        static_cast<int64_t>(hardware.counts[1]);
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxSetNode(
                sandbox.get(), source, &source_node),
            "Tensorless_FpmSandboxSetNode(QPU result source)"))
        return false;

    const uint64_t node_count =
        Tensorless_FpmSandboxNodeCount(sandbox.get());
    std::vector<uint64_t> actions(
        static_cast<size_t>(node_count), 0);
    std::vector<uint64_t> payloads(
        static_cast<size_t>(node_count), 0);
    payloads[source] = hardware.shots;
    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(
            Tensorless_FpmSandboxStep(
                sandbox.get(), actions.data(), payloads.data(),
                node_count, &stats),
            "Tensorless_FpmSandboxStep(QPU result)"))
        return false;

    result = {};
    for (uint32_t detector = 0; detector < 2; ++detector) {
        TensorlessFpmSandboxNode node{};
        if (!tensorless_adapter::Check(
                Tensorless_FpmSandboxGetNode(
                    sandbox.get(), detectors[detector], &node),
                "Tensorless_FpmSandboxGetNode(QPU result detector)"))
            return false;
        result.accepted[detector] = node.energy_subunits;
    }
    result.external_exhaust =
        stats.external_energy_exhaust_subunits;
    result.energy_residual = stats.energy_conservation_residual;
    for (uint32_t row = 0; row < 3; ++row)
        for (uint32_t column = 0; column < 3; ++column)
            result.momentum_residual[row][column] =
                stats.momentum_conservation_residual[row][column];
    return tensorless_adapter::ResidualsAreZero(stats) &&
           result.accepted == hardware.counts &&
           result.external_exhaust == 0;
}

std::string JsonString(std::string_view value) {
    std::string escaped = "\"";
    for (const char character : value) {
        if (character == '"' || character == '\\')
            escaped.push_back('\\');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

void WriteAudit(
    const HardwareResult& hardware,
    const Metrics& metrics,
    const RoutingResult& routing) {
    std::cout
        << "{\"type\":\"qpu_result\","
        << "\"provider\":" << JsonString(hardware.provider) << ","
        << "\"backend\":" << JsonString(hardware.backend) << ","
        << "\"job_id\":" << JsonString(hardware.job_id) << ","
        << "\"circuit\":" << JsonString(hardware.circuit) << ","
        << "\"identity_cycles\":" << hardware.identity_cycles << ","
        << "\"logical_circuit_sha256\":"
        << JsonString(hardware.logical_circuit_sha256) << ","
        << "\"transpiled_circuit_sha256\":"
        << JsonString(hardware.transpiled_circuit_sha256) << ","
        << "\"submitted_at_utc\":"
        << JsonString(hardware.submitted_at_utc) << ","
        << "\"completed_at_utc\":"
        << JsonString(hardware.completed_at_utc) << ","
        << "\"calibration_timestamp_utc\":";
    if (hardware.calibration_timestamp_utc.empty())
        std::cout << "null";
    else
        std::cout << JsonString(hardware.calibration_timestamp_utc);
    std::cout
        << ",\"isa_gate_counts\":"
        << hardware.isa_gate_counts_json << ","
        << "\"qiskit_version\":"
        << JsonString(hardware.qiskit_version) << ","
        << "\"qiskit_ibm_runtime_version\":"
        << JsonString(hardware.qiskit_ibm_runtime_version) << ","
        << "\"synthetic\":"
        << (hardware.synthetic ? "true" : "false") << ","
        << "\"shots\":" << hardware.shots << ","
        << "\"counts\":["
        << hardware.counts[0] << "," << hardware.counts[1] << "],"
        << "\"ideal_probability_q32\":["
        << kIdealZeroQ32 << ","
        << kQ32Scale - kIdealZeroQ32 << "],"
        << "\"ideal_counts\":["
        << metrics.ideal_counts[0] << ","
        << metrics.ideal_counts[1] << "],"
        << "\"signed_count_residuals\":["
        << metrics.signed_residuals[0] << ","
        << metrics.signed_residuals[1] << "],"
        << "\"l1_count_distance\":"
        << metrics.l1_count_distance << ","
        << "\"total_variation_q32\":"
        << metrics.total_variation_q32 << ","
        << "\"hellinger_q32\":"
        << metrics.hellinger_q32 << ","
        << "\"hellinger_squared_q32\":"
        << metrics.hellinger_squared_q32 << ","
        << "\"metric_scale\":" << kQ32Scale << ","
        << "\"metric_rounding\":\"deterministic_integer\","
        << "\"dynamical_decoupling_enabled\":"
        << (hardware.dynamical_decoupling_enabled ? "true" : "false")
        << ",\"twirling_enabled\":"
        << (hardware.twirling_enabled ? "true" : "false")
        << ",\"optimization_level\":"
        << hardware.optimization_level << "}\n"
        << "{\"type\":\"routing\","
        << "\"accepted\":["
        << routing.accepted[0] << ","
        << routing.accepted[1] << "],"
        << "\"detector_capacity_exhaust\":"
        << routing.external_exhaust << ","
        << "\"energy_residual\":"
        << routing.energy_residual << ","
        << "\"count_residual_is_energy\":false,"
        << "\"decoherence_inferred\":false}\n";
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: tensorless_qpu_result_adapter RESULT.json\n";
        return 1;
    }
    const std::optional<std::string> document = ReadFile(argv[1]);
    HardwareResult hardware{};
    if (!document.has_value() ||
        !ParseHardwareResult(*document, hardware)) {
        std::cerr << "invalid canonical QPU result\n";
        return 2;
    }
    const Metrics metrics = ComputeMetrics(hardware);
    RoutingResult routing{};
    if (!RouteCounts(hardware, routing))
        return 3;
    WriteAudit(hardware, metrics, routing);
    return 0;
}
