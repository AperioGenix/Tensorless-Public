#pragma once

#include <tensorless/tensorless.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace tensorless_adapter {

inline constexpr uint32_t kScale = 1000000U;
inline constexpr uint64_t kEnergyCeiling = 2000000ULL;

inline TensorlessFpmSandboxCreateInfo IsotropicConfig(
    uint32_t size_x, uint32_t size_y, uint32_t size_z) {
    TensorlessFpmSandboxCreateInfo info{};
    info.size_x = size_x;
    info.size_y = size_y;
    info.size_z = size_z;
    info.energy_ceiling_subunits = kEnergyCeiling;
    info.policy_scale = kScale;
    info.minimum_activity_weight = 1;
    info.isotropic_face_weight = 1;
    info.causal_energy_threshold = kEnergyCeiling + 1;
    info.causal_weight_threshold = kEnergyCeiling + 1;
    info.normal_momentum_fraction = kScale;
    info.causal_momentum_fraction = kScale;
    return info;
}

inline TensorlessFpmSandboxCreateInfo DirectionalConfig(
    uint32_t size_x, uint32_t size_y, uint32_t size_z) {
    TensorlessFpmSandboxCreateInfo info =
        IsotropicConfig(size_x, size_y, size_z);
    info.isotropic_face_weight = 0;
    info.causal_energy_threshold = 0;
    info.causal_weight_threshold = 1;
    return info;
}

inline uint64_t Flat(
    uint32_t size_y, uint32_t size_z,
    uint32_t x, uint32_t y, uint32_t z) {
    return uint64_t(x) * size_y * size_z + uint64_t(y) * size_z + z;
}

inline bool Check(TensorlessStatus status, const char* operation) {
    if (status == TENSORLESS_STATUS_OK) return true;
    std::cerr << operation << " failed with status "
              << static_cast<int>(status) << "\n";
    return false;
}

inline bool ReadNodes(
    TensorlessFpmSandbox* sandbox,
    std::vector<TensorlessFpmSandboxNode>& nodes) {
    const uint64_t count = Tensorless_FpmSandboxNodeCount(sandbox);
    nodes.resize(static_cast<size_t>(count));
    for (uint64_t index = 0; index < count; ++index) {
        if (!Check(
                Tensorless_FpmSandboxGetNode(
                    sandbox, index, &nodes[static_cast<size_t>(index)]),
                "Tensorless_FpmSandboxGetNode"))
            return false;
    }
    return true;
}

inline bool ResidualsAreZero(const TensorlessFpmSandboxStats& stats) {
    if (stats.energy_conservation_residual != 0) return false;
    for (uint32_t row = 0; row < TENSORLESS_FPM_AXES; ++row)
        for (uint32_t column = 0; column < TENSORLESS_FPM_AXES; ++column)
            if (stats.momentum_conservation_residual[row][column] != 0)
                return false;
    return true;
}

class Sandbox {
public:
    ~Sandbox() { Tensorless_FpmSandboxDestroy(value_); }

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;
    Sandbox() = default;

    bool Create(const TensorlessFpmSandboxCreateInfo& info) {
        return Check(
            Tensorless_FpmSandboxCreate(&info, &value_),
            "Tensorless_FpmSandboxCreate");
    }

    TensorlessFpmSandbox* get() const { return value_; }

private:
    TensorlessFpmSandbox* value_ = nullptr;
};

}  // namespace tensorless_adapter
