#pragma once

#include "tensorless/export.h"
#include <stdint.h>

typedef enum TensorlessStatus {
    TENSORLESS_STATUS_OK = 0,
    TENSORLESS_STATUS_INVALID_ARGUMENT = 1,
    TENSORLESS_STATUS_OUT_OF_MEMORY = 2,
    TENSORLESS_STATUS_BUFFER_TOO_SMALL = 3,
    TENSORLESS_STATUS_NOT_FOUND = 4,
    TENSORLESS_STATUS_STATE_ERROR = 5,
    TENSORLESS_STATUS_LIMIT_EXCEEDED = 6,
    TENSORLESS_STATUS_INVALID_HANDLE = 7
} TensorlessStatus;

enum {
    TENSORLESS_FPM_CHANNELS = 9,
    TENSORLESS_FPM_AXES = 3,
    TENSORLESS_FPM_NEIGHBORS = 6
};

typedef enum TensorlessFpmReplenishmentPolicy {
    TENSORLESS_FPM_REPLENISHMENT_PARAMETERIZED = 0,
    TENSORLESS_FPM_REPLENISHMENT_CONSTITUTIVE_R1 = 1
} TensorlessFpmReplenishmentPolicy;

typedef enum TensorlessFpmRouteExchangePolicy {
    TENSORLESS_FPM_ROUTE_EXCHANGE_DISABLED = 0,
    TENSORLESS_FPM_ROUTE_EXCHANGE_CONSTITUTIVE_N1 = 1
} TensorlessFpmRouteExchangePolicy;

/* All dynamic state is integral. Costs use micro-action units (1 action = 1e6). */
typedef struct TensorlessDaemon {
    int32_t psi[TENSORLESS_FPM_CHANNELS];
    uint64_t E;
    int32_t b;
    uint32_t R[TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    uint64_t R_bulk[TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    uint64_t R_boundary_out[TENSORLESS_FPM_NEIGHBORS][TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    uint64_t R_boundary_in[TENSORLESS_FPM_NEIGHBORS][TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    uint64_t next_step;
    uint8_t active;
    uint8_t consolidated;
    uint8_t reserved[6];
    uint32_t omega_prev;
    uint32_t tau;
    uint32_t pi;
    int32_t psi_imag[TENSORLESS_FPM_CHANNELS];
} TensorlessDaemon;

typedef struct TensorlessFpmConstants {
    uint64_t bit_equivalent_capacity;
    uint32_t cost_scale;
    uint32_t c0;
    uint32_t l_max;
    uint32_t gamma_max_milli;
    uint32_t depletion_numerator;
    uint32_t depletion_denominator;
    uint32_t percolation_floor;
    uint32_t omega_scale;
    uint32_t omega_min;
    uint32_t omega_max;
    uint64_t energy_max_numerator;
    uint32_t energy_max_denominator;
    uint32_t lambda_numerator;
    uint32_t lambda_denominator;
    uint32_t alpha_numerator;
    uint32_t beta_numerator;
    uint32_t channel_exponent_denominator;
    uint32_t carrier_scale;
} TensorlessFpmConstants;

typedef struct TensorlessFpmStats {
    uint64_t now;
    uint64_t events_processed;
    uint64_t active_daemons;
    uint64_t halted_daemons;
    uint64_t total_energy;
    uint64_t consolidations;
    uint64_t l_max_rejections;
    uint64_t holographic_2d_transitions;
    uint64_t total_action_processed;
    uint64_t transported_action;
    uint64_t irreversible_dissipation;
    int64_t total_conservation_residual;
} TensorlessFpmStats;

typedef struct TensorlessRgResidual {
    uint64_t energy;
    uint64_t carrier;
    uint64_t routing_face;
    uint64_t routing_bulk;
    uint64_t total;
} TensorlessRgResidual;

typedef struct TensorlessFluxEvent {
    uint64_t macro_block_id;
    uint8_t arrival_phase;
    uint8_t face;
    uint8_t from_axis;
    uint8_t to_axis;
    uint64_t energy;
    uint64_t shear;
} TensorlessFluxEvent;

/*
 * Experimental synchronous sandbox values are exact 1/3-micro-action
 * subunits.  Every operational policy coefficient is caller supplied; these
 * fields are not claimed as uniquely derived FPM constants.
 */
typedef struct TensorlessFpmSandboxCreateInfo {
    uint32_t size_x;
    uint32_t size_y;
    uint32_t size_z;
    uint64_t initial_energy_subunits;
    uint64_t energy_ceiling_subunits;
    uint32_t policy_scale;
    uint32_t activity_route_coefficient;
    uint32_t activity_geometry_coefficient;
    uint64_t minimum_activity_weight;
    uint64_t isotropic_face_weight;
    uint64_t causal_energy_threshold;
    uint64_t causal_weight_threshold;
    uint32_t normal_momentum_fraction;
    uint32_t causal_momentum_fraction;
    uint64_t causal_lock_ticks;
    uint32_t replenishment_policy;
    uint32_t route_exchange_policy;
    uint64_t route_exchange_seed;
    uint64_t route_exchange_stream;
} TensorlessFpmSandboxCreateInfo;

typedef struct TensorlessFpmSandboxNode {
    uint64_t energy_subunits;
    uint32_t route_cost[TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    int64_t momentum[TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    uint32_t tau;
    uint32_t pi;
    uint32_t omega;
    uint32_t mobility;
    uint64_t locked_until_tick;
} TensorlessFpmSandboxNode;

typedef struct TensorlessFpmSandboxStats {
    uint64_t tick;
    uint64_t total_energy_subunits;
    uint64_t external_energy_exhaust_subunits;
    uint64_t starvation_deficit_subunits;
    int64_t external_momentum_exhaust[TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    int64_t energy_conservation_residual;
    int64_t momentum_conservation_residual[TENSORLESS_FPM_AXES][TENSORLESS_FPM_AXES];
    uint64_t route_exchange_trials;
    uint64_t route_exchanges_applied;
    uint64_t route_exchanges_vetoed;
} TensorlessFpmSandboxStats;

/* ── Parameter Ledger (audit trail) ─────────────────────────── */

enum TensorlessParamClass {
    TENSORLESS_PARAM_FIXED   = 0,  /* locked before run, never changes */
    TENSORLESS_PARAM_DERIVED = 1,  /* computed from fixed parameters */
    TENSORLESS_PARAM_FREE    = 2,  /* adapter can set any value */
    TENSORLESS_PARAM_FITTED  = 3   /* adjusted to match data */
};

typedef struct {
    char name[64];
    double value;
    enum TensorlessParamClass classification;
    char source[128];     /* "core", "adapter:pathfinding", etc. */
} TensorlessParamEntry;

typedef struct {
    TensorlessParamEntry* entries;
    uint32_t count;
    uint32_t capacity;
    char core_version[32];
    uint64_t core_constants_hash;
    uint64_t adapter_hash;
    uint64_t input_hash;
    uint32_t free_parameter_count;
    uint32_t fitted_parameter_count;
    uint32_t fixed_parameter_count;
    uint32_t derived_parameter_count;
} TensorlessParamLedger;
