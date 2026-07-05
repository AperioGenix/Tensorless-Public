#pragma once

#include "tensorless/export.h"
#include "tensorless/types.h"
#include <stdio.h>

typedef struct TensorlessHandle TensorlessHandle;
typedef struct TensorlessFpmSandbox TensorlessFpmSandbox;

typedef struct TensorlessCreateInfo {
    uint32_t size_x;
    uint32_t size_y;
    uint32_t size_z;
    uint64_t initial_energy;
} TensorlessCreateInfo;

#if defined(__cplusplus)
extern "C" {
#endif

TENSORLESS_EXTERN_C TENSORLESS_API void TENSORLESS_CALL Tensorless_GetDefaultCreateInfo(TensorlessCreateInfo* out_info);
TENSORLESS_EXTERN_C TENSORLESS_API void TENSORLESS_CALL Tensorless_GetFpmConstants(TensorlessFpmConstants* out_constants);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_Create(const TensorlessCreateInfo* info, TensorlessHandle** out_handle);
TENSORLESS_EXTERN_C TENSORLESS_API void TENSORLESS_CALL Tensorless_Destroy(TensorlessHandle* handle);
TENSORLESS_EXTERN_C TENSORLESS_API void TENSORLESS_CALL Tensorless_Reset(TensorlessHandle* handle);
TENSORLESS_EXTERN_C TENSORLESS_API uint64_t TENSORLESS_CALL Tensorless_DaemonCount(const TensorlessHandle* handle);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_GetDaemon(const TensorlessHandle* handle, uint64_t flat_idx, TensorlessDaemon* out_daemon);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_SetDaemon(TensorlessHandle* handle, uint64_t flat_idx, const TensorlessDaemon* daemon);
/*
 * Computes the 1D index for a 3D coordinate.
 * Coordinates strictly wrap modulo the axis dimension (periodic torus topology).
 * Negative coordinates mathematically wrap to the opposite boundary.
 * Returns INVALID_HANDLE for an invalid handle and INVALID_ARGUMENT when
 * out_index is null.
 */
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FlatIndex(const TensorlessHandle* handle, int64_t x, int64_t y, int64_t z, uint64_t* out_index);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_Neighbors6(const TensorlessHandle* handle, uint64_t flat_idx, uint64_t out_neighbors[TENSORLESS_FPM_NEIGHBORS]);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_SetRouteCost(TensorlessHandle* handle, uint64_t flat_idx, uint32_t from_axis, uint32_t to_axis, uint32_t cost);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_SetBulkRouteCost(TensorlessHandle* handle, uint64_t flat_idx, uint32_t from_axis, uint32_t to_axis, uint64_t cost);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_Schedule(TensorlessHandle* handle, uint64_t flat_idx, uint64_t next_step);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_Run(TensorlessHandle* handle, uint64_t max_events, TensorlessFpmStats* out_stats);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_GetStats(const TensorlessHandle* handle, TensorlessFpmStats* out_stats);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_GetRouteAction(const TensorlessDaemon* daemon, uint64_t* out_action);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_CoarseGrain(const TensorlessHandle* source, uint32_t block_width, TensorlessHandle** out_handle);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_EnableFluxRecording(TensorlessHandle* handle, uint32_t block_width);
TENSORLESS_EXTERN_C TENSORLESS_API uint64_t TENSORLESS_CALL Tensorless_FluxEventCount(const TensorlessHandle* handle);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_CopyFluxEvents(const TensorlessHandle* handle, TensorlessFluxEvent* out_events, uint64_t capacity);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_CopyPhaseFlux(const TensorlessHandle* handle, uint64_t* out_values, uint64_t capacity);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_EnableRadialFluxMeasurement(TensorlessHandle* handle, uint32_t center_x, uint32_t center_y, uint32_t center_z, uint32_t manhattan_radius);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_GetRadialFluxMeasurement(const TensorlessHandle* handle, uint64_t* out_accepted_energy);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_EnableRadialFluxMeasurements(TensorlessHandle* handle, uint32_t center_x, uint32_t center_y, uint32_t center_z, const uint32_t* manhattan_radii, uint32_t radius_count);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_CopyRadialFluxMeasurements(const TensorlessHandle* handle, uint64_t* out_accepted_energy, uint32_t capacity);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_CopyRadialFluxMeasurementsBidirectional(const TensorlessHandle* handle, uint64_t* out_accepted_energy_outward, uint64_t* out_accepted_energy_inward, uint32_t capacity);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FpmSandboxCreate(const TensorlessFpmSandboxCreateInfo* info, TensorlessFpmSandbox** out_sandbox);
TENSORLESS_EXTERN_C TENSORLESS_API void TENSORLESS_CALL Tensorless_FpmSandboxDestroy(TensorlessFpmSandbox* sandbox);
TENSORLESS_EXTERN_C TENSORLESS_API uint64_t TENSORLESS_CALL Tensorless_FpmSandboxNodeCount(const TensorlessFpmSandbox* sandbox);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FpmSandboxGetNode(const TensorlessFpmSandbox* sandbox, uint64_t flat_idx, TensorlessFpmSandboxNode* out_node);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FpmSandboxSetNode(TensorlessFpmSandbox* sandbox, uint64_t flat_idx, const TensorlessFpmSandboxNode* node);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FpmSandboxSetObservables(TensorlessFpmSandbox* sandbox, uint64_t flat_idx, uint32_t omega, uint32_t mobility, uint32_t pi, uint32_t tau);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FpmSandboxStep(TensorlessFpmSandbox* sandbox, const uint64_t* action_subunits, const uint64_t* payload_subunits, uint64_t count, TensorlessFpmSandboxStats* out_stats);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_FpmSandboxGetStats(const TensorlessFpmSandbox* sandbox, TensorlessFpmSandboxStats* out_stats);
/* ── Parameter Ledger API ───────────────────────────────────── */
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_ParamLedgerInit(TensorlessParamLedger* ledger);
TENSORLESS_EXTERN_C TENSORLESS_API void TENSORLESS_CALL Tensorless_ParamLedgerDestroy(TensorlessParamLedger* ledger);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_ParamLedgerAdd(TensorlessParamLedger* ledger, const char* name, double value, enum TensorlessParamClass cls, const char* source);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_ParamLedgerAddCoreConstants(TensorlessParamLedger* ledger);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_ParamLedgerDump(const TensorlessParamLedger* ledger, FILE* f);
TENSORLESS_EXTERN_C TENSORLESS_API TensorlessStatus TENSORLESS_CALL Tensorless_ParamLedgerDumpJson(const TensorlessParamLedger* ledger, FILE* f);
TENSORLESS_EXTERN_C TENSORLESS_API uint64_t TENSORLESS_CALL Tensorless_ParamLedgerHash(const TensorlessParamLedger* ledger);

#if defined(__cplusplus)
}
#endif
