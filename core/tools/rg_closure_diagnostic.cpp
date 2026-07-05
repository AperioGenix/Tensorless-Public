#include "tensorless/tensorless.h"

#include <cstdint>
#include <cstdio>

// Diagnostic only: exact RG closure is not a consensus invariant. Coarse
// graining discards causal ordering, so a nonzero residual is expected when
// microscopic routing is order-sensitive. Keep this executable outside CTest.
namespace {
uint32_t Next(uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

uint64_t Difference(uint64_t a, uint64_t b) { return a > b ? a-b : b-a; }
uint64_t Difference(int32_t a, int32_t b) {
    const int64_t delta = int64_t(a)-int64_t(b);
    return delta < 0 ? uint64_t(-delta) : uint64_t(delta);
}

bool Initialize(uint32_t width, TensorlessHandle** out) {
    TensorlessCreateInfo info{width,width,width,100000000ULL};
    if (Tensorless_Create(&info,out) != TENSORLESS_STATUS_OK) return false;
    uint32_t rng=0x72656e6fU;
    for (uint64_t i=0;i<Tensorless_DaemonCount(*out);++i) {
        TensorlessDaemon daemon{};
        if (Tensorless_GetDaemon(*out,i,&daemon) != TENSORLESS_STATUS_OK) return false;
        daemon.E=90000000ULL+(Next(rng)%10000001U);
        for (uint32_t ch=0;ch<TENSORLESS_FPM_CHANNELS;++ch)
            daemon.psi[ch]=int32_t(Next(rng)%7U)-3;
        for (uint32_t a=0;a<3;++a)
            for (uint32_t b=0;b<3;++b)
                daemon.R[a][b]=20000U+(Next(rng)%30001U);
        if (Tensorless_SetDaemon(*out,i,&daemon) != TENSORLESS_STATUS_OK) return false;
    }
    return true;
}

TensorlessRgResidual Compare(const TensorlessDaemon& lhs,const TensorlessDaemon& rhs) {
    TensorlessRgResidual residual{};
    residual.energy=Difference(lhs.E,rhs.E);
    for(uint32_t ch=0;ch<TENSORLESS_FPM_CHANNELS;++ch)
        residual.carrier+=Difference(lhs.psi[ch],rhs.psi[ch]);
    for(uint32_t a=0;a<3;++a)
        for(uint32_t b=0;b<3;++b) {
            residual.routing_face+=Difference(uint64_t(lhs.R[a][b]),uint64_t(rhs.R[a][b]));
            residual.routing_bulk+=Difference(lhs.R_bulk[a][b],rhs.R_bulk[a][b]);
        }
    residual.total=residual.energy+residual.carrier+residual.routing_face+residual.routing_bulk;
    return residual;
}

TensorlessRgResidual RunClosure(uint32_t width,TensorlessFpmStats& micro_stats,
                                TensorlessFpmStats& macro_stats) {
    TensorlessHandle* micro=nullptr;
    TensorlessHandle* macro_initial=nullptr;
    TensorlessHandle* macro_path1=nullptr;
    if(!Initialize(width,&micro)) return {~0ULL,0,0,0,~0ULL};
    if(Tensorless_CoarseGrain(micro,width,&macro_initial)!=TENSORLESS_STATUS_OK)
        return {~0ULL,0,0,0,~0ULL};
    const uint64_t micro_events=Tensorless_DaemonCount(micro)*width;
    Tensorless_Run(micro,micro_events,&micro_stats);
    if(Tensorless_CoarseGrain(micro,width,&macro_path1)!=TENSORLESS_STATUS_OK)
        return {~0ULL,0,0,0,~0ULL};
    Tensorless_Run(macro_initial,1,&macro_stats);
    TensorlessDaemon path1{},path2{};
    Tensorless_GetDaemon(macro_path1,0,&path1);
    Tensorless_GetDaemon(macro_initial,0,&path2);
    const TensorlessRgResidual residual=Compare(path1,path2);
    Tensorless_Destroy(macro_path1);
    Tensorless_Destroy(macro_initial);
    Tensorless_Destroy(micro);
    return residual;
}
}

int main() {
    bool closed=true;
    const uint32_t widths[2]{2U,3U};
    for(uint32_t width:widths) {
        TensorlessFpmStats micro{},macro{};
        const TensorlessRgResidual residual=RunClosure(width,micro,macro);
        std::printf(
            "B=%u energy=%llu carrier=%llu face=%llu bulk=%llu total=%llu "
            "micro_conservation=%lld macro_conservation=%lld "
            "lmax_rejections=%llu transitions=%llu\n",
            width,
            static_cast<unsigned long long>(residual.energy),
            static_cast<unsigned long long>(residual.carrier),
            static_cast<unsigned long long>(residual.routing_face),
            static_cast<unsigned long long>(residual.routing_bulk),
            static_cast<unsigned long long>(residual.total),
            static_cast<long long>(micro.total_conservation_residual),
            static_cast<long long>(macro.total_conservation_residual),
            static_cast<unsigned long long>(micro.l_max_rejections+macro.l_max_rejections),
            static_cast<unsigned long long>(
                micro.holographic_2d_transitions+macro.holographic_2d_transitions));
        closed=closed && residual.total==0;
    }
    return closed?0:1;
}
