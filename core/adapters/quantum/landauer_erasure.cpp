#include "../common/sandbox_adapter.h"

#include <iostream>
#include <vector>

namespace {

constexpr uint64_t kQuantum = 1000;
constexpr uint64_t kLandauerActionSubunits = 2805; // kT ln 2 at 298K scaled

struct RunResult {
    bool sufficient_energy_success = false;
    uint64_t case_a_deficit = 0;
    
    bool starvation_deficit_success = false;
    uint64_t case_b_deficit = 0;
    
    bool operator==(const RunResult&) const = default;
};

bool RunCaseA(RunResult& result) {
    tensorless_adapter::Sandbox sandbox;
    TensorlessFpmSandboxCreateInfo info = tensorless_adapter::IsotropicConfig(2, 1, 1);
    if (!sandbox.Create(info)) return false;

    const uint64_t source = tensorless_adapter::Flat(1, 1, 0, 0, 0);
    const uint64_t sink = tensorless_adapter::Flat(1, 1, 1, 0, 0);

    // Case A: Sufficient Energy
    TensorlessFpmSandboxNode source_node{};
    source_node.energy_subunits = kQuantum + kLandauerActionSubunits;
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxSetNode(sandbox.get(), source, &source_node), "SetNode source")) return false;

    TensorlessFpmSandboxNode sink_node{};
    sink_node.energy_subunits = tensorless_adapter::kEnergyCeiling - kQuantum;
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxSetNode(sandbox.get(), sink, &sink_node), "SetNode sink")) return false;

    std::vector<uint64_t> actions(2, 0);
    std::vector<uint64_t> payloads(2, 0);
    actions[source] = kLandauerActionSubunits;
    payloads[source] = kQuantum;

    TensorlessFpmSandboxStats stats{};
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxStep(sandbox.get(), actions.data(), payloads.data(), 2, &stats), "Step Case A")) return false;

    TensorlessFpmSandboxNode final_source{};
    TensorlessFpmSandboxNode final_sink{};
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxGetNode(sandbox.get(), source, &final_source), "GetNode source")) return false;
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxGetNode(sandbox.get(), sink, &final_sink), "GetNode sink")) return false;

    result.case_a_deficit = stats.starvation_deficit_subunits;
    result.sufficient_energy_success = (stats.starvation_deficit_subunits == 0 && final_source.energy_subunits == kLandauerActionSubunits && final_sink.energy_subunits == tensorless_adapter::kEnergyCeiling);
    return true;
}

bool RunCaseB(RunResult& result) {
    tensorless_adapter::Sandbox sandbox;
    TensorlessFpmSandboxCreateInfo info = tensorless_adapter::IsotropicConfig(2, 1, 1);
    if (!sandbox.Create(info)) return false;

    const uint64_t source = tensorless_adapter::Flat(1, 1, 0, 0, 0);
    const uint64_t sink = tensorless_adapter::Flat(1, 1, 1, 0, 0);

    // Case B: Starvation Deficit
    TensorlessFpmSandboxNode source_node{};
    // We intentionally deprive the node of the Landauer cost. It only has the payload energy.
    source_node.energy_subunits = kQuantum;
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxSetNode(sandbox.get(), source, &source_node), "SetNode source")) return false;

    TensorlessFpmSandboxNode sink_node{};
    sink_node.energy_subunits = tensorless_adapter::kEnergyCeiling - kQuantum;
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxSetNode(sandbox.get(), sink, &sink_node), "SetNode sink")) return false;

    std::vector<uint64_t> actions(2, 0);
    std::vector<uint64_t> payloads(2, 0);
    actions[source] = kLandauerActionSubunits;
    payloads[source] = kQuantum;

    TensorlessFpmSandboxStats stats{};
    // The engine should NOT throw TENSORLESS_STATUS_STATE_ERROR here. 
    // It should return TENSORLESS_STATUS_OK, gracefully fail the routing, and record the deficit.
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxStep(sandbox.get(), actions.data(), payloads.data(), 2, &stats), "Step Case B")) return false;

    TensorlessFpmSandboxNode final_source{};
    TensorlessFpmSandboxNode final_sink{};
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxGetNode(sandbox.get(), source, &final_source), "GetNode source")) return false;
    if (!tensorless_adapter::Check(Tensorless_FpmSandboxGetNode(sandbox.get(), sink, &final_sink), "GetNode sink")) return false;

    result.case_b_deficit = stats.starvation_deficit_subunits;
    // Because actual_payload becomes 0, the node uses its 1000 subunits to pay for the 2805 action, 
    // starving by 1805. The payload does not reach the sink.
    result.starvation_deficit_success = (stats.starvation_deficit_subunits > 0 && 
                                         final_source.energy_subunits == 0 &&
                                         final_sink.energy_subunits == tensorless_adapter::kEnergyCeiling - kQuantum);
    return true;
}

void WriteTrace(const RunResult& result) {
    std::cout
        << "{\"type\":\"safety_case\","
        << "\"name\":\"sufficient_energy_case\","
        << "\"starvation_deficit\":" << result.case_a_deficit << ","
        << "\"erasure_successful\":" << (result.sufficient_energy_success ? "true" : "false")
        << "}\n"
        << "{\"type\":\"safety_case\","
        << "\"name\":\"starvation_deficit_case\","
        << "\"starvation_deficit\":" << result.case_b_deficit << ","
        << "\"erasure_failed\":" << (result.starvation_deficit_success ? "true" : "false")
        << "}\n"
        << "{\"type\":\"summary\","
        << "\"adapter\":\"landauer_erasure_validator\","
        << "\"quantum\":1000,"
        << "\"landauer_action_subunits\":2805,"
        << "\"thermodynamic_bounds_enforced\":true}\n";
}

} // namespace

int main() {
    RunResult result{};
    if (!RunCaseA(result) || !RunCaseB(result)) {
        return 2;
    }
    
    if (!result.sufficient_energy_success || !result.starvation_deficit_success) {
        std::cerr << "Thermodynamic bounding failed. Sufficient: " << result.sufficient_energy_success 
                  << ", Starvation: " << result.starvation_deficit_success << "\n";
        return 1;
    }
    
    WriteTrace(result);
    return 0;
}
