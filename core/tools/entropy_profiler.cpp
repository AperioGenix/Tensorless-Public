#include "tensorless/tensorless.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kBlock = 3;
constexpr uint32_t kStates = 1000;
constexpr uint32_t kBins = kBlock * TENSORLESS_FPM_NEIGHBORS;

uint32_t Next(uint32_t& state) {
    state = state * 1664525U + 1013904223U;
    return state;
}

bool InitializeState(uint32_t seed, TensorlessHandle** out) {
    TensorlessCreateInfo info{kBlock,kBlock,kBlock,100000000ULL};
    if (Tensorless_Create(&info,out) != TENSORLESS_STATUS_OK) return false;
    uint32_t rng=seed;
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
    return Tensorless_EnableFluxRecording(*out,kBlock)==TENSORLESS_STATUS_OK;
}

uint64_t PhaseIndex(uint32_t phase,uint32_t face) {
    return (((uint64_t(phase)*6ULL+face)*3ULL+0ULL)*3ULL+1ULL);
}
}

int main(int argc,char** argv) {
    const std::filesystem::path output=argc>1?argv[1]:"core/artifacts/rg_entropy_b3.jsonl";
    if(!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output);
    if(!out){std::cerr<<"Cannot write "<<output<<"\n";return 2;}

    std::vector<int64_t> residuals;
    residuals.reserve(uint64_t(kStates)*kBins);
    uint64_t total_events=0;
    for(uint32_t sample=0;sample<kStates;++sample) {
        TensorlessHandle* micro=nullptr;
        TensorlessHandle* macro=nullptr;
        if(!InitializeState(0x9e3779b9U^sample,&micro) ||
           Tensorless_CoarseGrain(micro,kBlock,&macro)!=TENSORLESS_STATUS_OK) {
            std::cerr<<"Initialization failed at sample "<<sample<<"\n";
            Tensorless_Destroy(macro);
            Tensorless_Destroy(micro);
            return 3;
        }
        TensorlessDaemon initial{};
        Tensorless_GetDaemon(macro,0,&initial);
        TensorlessFpmStats stats{};
        Tensorless_Run(micro,uint64_t(kBlock)*kBlock*kBlock*kBlock,&stats);
        std::vector<uint64_t> phase_flux(uint64_t(kBlock)*6ULL*3ULL*3ULL);
        Tensorless_CopyPhaseFlux(micro,phase_flux.data(),phase_flux.size());
        const uint64_t event_count=Tensorless_FluxEventCount(micro);
        total_events+=event_count;

        uint64_t boundary_total=0;
        for(uint32_t face=0;face<6;++face)
            boundary_total+=initial.R_boundary_out[face][0][1];
        const uint64_t linear_total=boundary_total+(initial.R_bulk[0][1]/kBlock);
        const uint64_t linear_bin=linear_total/kBins;

        out<<"{\"sample\":"<<sample
           <<",\"macro_energy\":"<<initial.E
           <<",\"bulk_01\":"<<initial.R_bulk[0][1]
           <<",\"boundary_01\":"<<boundary_total
           <<",\"flux_events\":"<<event_count
           <<",\"phase_face_shear\":[";
        bool first=true;
        for(uint32_t phase=0;phase<kBlock;++phase) {
            for(uint32_t face=0;face<6;++face) {
                const uint64_t observed=phase_flux[PhaseIndex(phase,face)];
                const int64_t residual=observed>=linear_bin
                    ? static_cast<int64_t>(observed-linear_bin)
                    : -static_cast<int64_t>(linear_bin-observed);
                residuals.push_back(residual);
                out<<(first?"":",")<<observed;
                first=false;
            }
        }
        out<<"]}\n";
        Tensorless_Destroy(macro);
        Tensorless_Destroy(micro);
    }

    int64_t residual_sum=0;
    for(int64_t value:residuals) residual_sum+=value;
    const int64_t mean=residual_sum/static_cast<int64_t>(residuals.size());
    uint64_t squared_deviation_sum=0;
    uint64_t maximum_absolute=0;
    for(int64_t value:residuals) {
        const int64_t delta=value-mean;
        const uint64_t magnitude=delta<0?uint64_t(-(delta+1))+1ULL:uint64_t(delta);
        squared_deviation_sum+=magnitude*magnitude;
        if(magnitude>maximum_absolute) maximum_absolute=magnitude;
    }
    const uint64_t variance=squared_deviation_sum/residuals.size();
    out<<"{\"summary\":{\"states\":"<<kStates
       <<",\"bins\":"<<residuals.size()
       <<",\"flux_events\":"<<total_events
       <<",\"mean_residual\":"<<mean
       <<",\"variance\":"<<variance
       <<",\"max_absolute_deviation\":"<<maximum_absolute
       <<"}}\n";
    std::cout<<"RG entropy profiling complete\n"
             <<"states="<<kStates<<" bins="<<residuals.size()
             <<" flux_events="<<total_events
             <<" mean_residual="<<mean
             <<" variance="<<variance
             <<" max_absolute_deviation="<<maximum_absolute<<"\n"
             <<"output="<<output.string()<<"\n";
    return 0;
}
