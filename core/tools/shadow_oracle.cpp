#include "tensorless/tensorless.h"

#include <charconv>
#include <cstdint>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kBlock=3;
constexpr uint32_t kMicroX=96,kMicroY=96,kMicroZ=12;
constexpr uint32_t kMacroX=32,kMacroY=32,kMacroZ=4;
constexpr uint32_t kDefaultTicks=34;

struct Key {
    uint8_t phase,face,bulk_bucket,incoming_bucket;
    bool operator<(const Key&r)const {
        if(phase!=r.phase)return phase<r.phase;
        if(face!=r.face)return face<r.face;
        if(bulk_bucket!=r.bulk_bucket)return bulk_bucket<r.bulk_bucket;
        return incoming_bucket<r.incoming_bucket;
    }
};
struct Range {
    uint64_t minimum=std::numeric_limits<uint64_t>::max(),maximum=0,count=0;
    void Add(uint64_t value){if(value<minimum)minimum=value;if(value>maximum)maximum=value;++count;}
};
struct ExactEntry {
    uint64_t fingerprint=0;
    Range range;
};
uint8_t LogBucket(uint64_t value){uint8_t b=0;while(value>1){value>>=1;++b;}return b;}
uint64_t Mix(uint64_t hash,uint64_t value) {
    value+=0x9e3779b97f4a7c15ULL;
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    value^=value>>31;
    return (hash^value)*1099511628211ULL;
}
uint64_t ExactHash(const TensorlessDaemon& state,const std::array<uint64_t,3>& pending,
                   uint32_t tick,uint32_t phase,uint32_t face,uint64_t seed) {
    uint64_t hash=seed;
    for(uint32_t i=0;i<3;++i)for(uint32_t j=0;j<3;++j)hash=Mix(hash,state.R_bulk[i][j]);
    for(uint32_t f=0;f<6;++f)for(uint32_t i=0;i<3;++i)for(uint32_t j=0;j<3;++j)
        hash=Mix(hash,state.R_boundary_in[f][i][j]);
    for(uint32_t f=0;f<6;++f)for(uint32_t i=0;i<3;++i)for(uint32_t j=0;j<3;++j)
        hash=Mix(hash,state.R_boundary_out[f][i][j]);
    for(uint32_t i=0;i<3;++i)for(uint32_t j=0;j<3;++j)hash=Mix(hash,state.R[i][j]);
    for(uint32_t channel=0;channel<TENSORLESS_FPM_CHANNELS;++channel)
        hash=Mix(hash,static_cast<uint32_t>(state.psi[channel]));
    for(uint32_t channel=0;channel<TENSORLESS_FPM_CHANNELS;++channel)
        hash=Mix(hash,static_cast<uint32_t>(state.psi_imag[channel]));
    for(uint64_t value:pending)hash=Mix(hash,value);
    hash=Mix(hash,state.E);
    hash=Mix(hash,static_cast<uint32_t>(state.b));
    hash=Mix(hash,state.omega_prev);
    hash=Mix(hash,state.tau);
    hash=Mix(hash,state.pi);
    hash=Mix(hash,state.next_step);
    hash=Mix(hash,state.active);
    hash=Mix(hash,state.consolidated);
    hash=Mix(hash,phase);hash=Mix(hash,face);hash=Mix(hash,tick);
    return hash;
}

bool AddTraceLoad(TensorlessDaemon& daemon,uint64_t load) {
    const uint64_t diagonal=load/3ULL;
    const uint64_t first=diagonal+load%3ULL;
    if(first>std::numeric_limits<uint64_t>::max()-daemon.R_bulk[0][0] ||
       diagonal>std::numeric_limits<uint64_t>::max()-daemon.R_bulk[1][1] ||
       diagonal>std::numeric_limits<uint64_t>::max()-daemon.R_bulk[2][2])return false;
    daemon.R_bulk[0][0]+=first;
    daemon.R_bulk[1][1]+=diagonal;
    daemon.R_bulk[2][2]+=diagonal;
    return true;
}

bool LiftMacroLoad(TensorlessHandle* oracle,uint32_t macro_x,uint32_t macro_y,
                   uint32_t macro_z,uint64_t load) {
    const uint64_t quotient=load/27ULL;
    const uint64_t remainder=load%27ULL;
    for(uint32_t local_x=0;local_x<3;++local_x){
        for(uint32_t local_y=0;local_y<3;++local_y){
            for(uint32_t local_z=0;local_z<3;++local_z){
                uint64_t index=0;
                if(Tensorless_FlatIndex(
                       oracle,macro_x*3U+local_x,macro_y*3U+local_y,
                       macro_z*3U+local_z,&index)!=TENSORLESS_STATUS_OK)return false;
                TensorlessDaemon daemon{};
                if(Tensorless_GetDaemon(oracle,index,&daemon)!=TENSORLESS_STATUS_OK ||
                   !AddTraceLoad(daemon,quotient))return false;
                if(local_x==1U&&local_y==1U&&local_z==1U &&
                   !AddTraceLoad(daemon,remainder))return false;
                if(Tensorless_SetDaemon(oracle,index,&daemon)!=TENSORLESS_STATUS_OK)return false;
            }
        }
    }
    return true;
}

bool ParsePositiveLoad(const char* text,uint64_t& load) {
    if(!text||!*text)return false;
    const char* end=text;
    while(*end)++end;
    const auto result=std::from_chars(text,end,load);
    return result.ec==std::errc{}&&result.ptr==end&&load!=0;
}
}

int main(int argc,char**argv){
    if(argc<2||argc>4) {
        std::cerr
            <<"Usage: shadow_oracle INJECTED_LOAD [OUTPUT_JSON] [TICKS]\n";
        return 2;
    }
    uint64_t injected_load=0;
    if(!ParsePositiveLoad(argv[1],injected_load)) {
        std::cerr<<"INJECTED_LOAD must be a positive uint64 integer\n";
        return 2;
    }
    const std::filesystem::path output=
        argc>=3?argv[2]:"core/artifacts/shadow_oracle.json";
    uint32_t ticks=kDefaultTicks;
    if(argc==4) {
        uint64_t parsed_ticks=0;
        if(!ParsePositiveLoad(argv[3],parsed_ticks)||
           parsed_ticks>std::numeric_limits<uint32_t>::max()) {
            std::cerr<<"TICKS must be a positive uint32 integer\n";
            return 2;
        }
        ticks=static_cast<uint32_t>(parsed_ticks);
    }
    TensorlessCreateInfo info{kMicroX,kMicroY,kMicroZ,100000000ULL};
    TensorlessHandle* oracle=nullptr;
    if(Tensorless_Create(&info,&oracle)!=TENSORLESS_STATUS_OK){
        std::cerr<<"Cannot allocate shadow oracle\n";return 2;
    }
    if(!LiftMacroLoad(oracle,16,16,2,injected_load)){
        std::cerr<<"Cannot lift macro load into shadow oracle\n";return 3;
    }
    uint64_t center=0;
    Tensorless_FlatIndex(oracle,49,49,7,&center);
    TensorlessDaemon injected{};
    Tensorless_GetDaemon(oracle,center,&injected);
    uint64_t injected_action=0;
    Tensorless_GetRouteAction(&injected,&injected_action);
    if(Tensorless_EnableFluxRecording(oracle,kBlock)!=TENSORLESS_STATUS_OK){
        std::cerr<<"Cannot initialize shadow oracle\n";return 3;
    }

    std::map<Key,Range> ranges;
    std::unordered_map<uint64_t,ExactEntry> exact_ranges;
    std::vector<std::array<uint64_t,3>> pending(uint64_t(kMacroX)*kMacroY*kMacroZ);
    uint64_t hash_collisions=0;
    uint64_t total_events=0,total_observations=0;
    for(uint32_t tick=0;tick<ticks;++tick){
        TensorlessHandle* macro=nullptr;
        if(Tensorless_CoarseGrain(oracle,kBlock,&macro)!=TENSORLESS_STATUS_OK){
            std::cerr<<"Coarse grain failed at tick "<<tick<<"\n";return 4;
        }
        std::vector<TensorlessDaemon> macro_states(uint64_t(kMacroX)*kMacroY*kMacroZ);
        for(uint64_t i=0;i<macro_states.size();++i)Tensorless_GetDaemon(macro,i,&macro_states[i]);
        Tensorless_Destroy(macro);

        Tensorless_EnableFluxRecording(oracle,kBlock);
        TensorlessFpmStats stats{};
        const uint64_t events=uint64_t(kMicroX)*kMicroY*kMicroZ*kBlock;
        Tensorless_Run(oracle,events,&stats);
        const uint64_t event_count=Tensorless_FluxEventCount(oracle);
        std::vector<TensorlessFluxEvent> flux_events(event_count);
        Tensorless_CopyFluxEvents(oracle,flux_events.data(),flux_events.size());
        total_events+=event_count;

        std::map<uint64_t,uint64_t> outcomes;
        for(const TensorlessFluxEvent& event:flux_events){
            const uint64_t outcome_key=(event.macro_block_id*3ULL+event.arrival_phase)*6ULL+event.face;
            outcomes[outcome_key]+=event.shear;
        }
        std::vector<std::array<uint64_t,3>> next_pending(pending.size());
        for(uint64_t block=0;block<macro_states.size();++block){
            const TensorlessDaemon& state=macro_states[block];
            for(uint32_t phase=0;phase<3;++phase){
                for(uint32_t face=0;face<6;++face){
                    const uint64_t outcome_key=(block*3ULL+phase)*6ULL+face;
                    const uint64_t value=outcomes[outcome_key];
                    const Key key{
                        static_cast<uint8_t>(phase),static_cast<uint8_t>(face),
                        LogBucket(state.R_bulk[0][1]),
                        LogBucket(state.R_boundary_in[face][0][1])
                    };
                    ranges[key].Add(value);
                    const uint64_t hash=ExactHash(
                        state,pending[block],tick,phase,face,1469598103934665603ULL);
                    const uint64_t fingerprint=ExactHash(
                        state,pending[block],tick,phase,face,0xd6e8feb86659fd93ULL);
                    auto [entry,inserted]=exact_ranges.try_emplace(hash);
                    if(inserted)entry->second.fingerprint=fingerprint;
                    else if(entry->second.fingerprint!=fingerprint)++hash_collisions;
                    entry->second.range.Add(value);
                    next_pending[block][phase]+=value;
                    ++total_observations;
                }
            }
        }
        pending=std::move(next_pending);
    }
    Tensorless_Destroy(oracle);

    uint64_t conflicting_keys=0,max_width=0,aliased_observations=0;
    for(const auto&[key,range]:ranges){
        const uint64_t width=range.maximum-range.minimum;
        if(width){++conflicting_keys;aliased_observations+=range.count;if(width>max_width)max_width=width;}
    }
    uint64_t exact_conflicts=0,exact_conflict_observations=0;
    uint64_t exact_max_width=0,exact_reused_keys=0,exact_reused_observations=0;
    for(const auto&[hash,entry]:exact_ranges) {
        const uint64_t width=entry.range.maximum-entry.range.minimum;
        if(width){
            ++exact_conflicts;
            exact_conflict_observations+=entry.range.count;
            if(width>exact_max_width)exact_max_width=width;
        }
        if(entry.range.count>1){++exact_reused_keys;exact_reused_observations+=entry.range.count;}
    }
    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output);
    if(!out){std::cerr<<"Cannot write output\n";return 5;}
    out<<"{\n  \"schema\":\"tensorless-shadow-oracle-v1\",\n"
       <<"  \"micro_size\":[96,96,12],\n  \"macro_size\":[32,32,4],\n"
       <<"  \"ticks\":"<<ticks
       <<",\n  \"injected_load\":"<<injected_load
       <<",\n  \"injected_action\":"<<injected_action
       <<",\n  \"flux_events\":"<<total_events
       <<",\n  \"observations\":"<<total_observations
       <<",\n  \"unique_keys\":"<<ranges.size()
       <<",\n  \"conflicting_keys\":"<<conflicting_keys
       <<",\n  \"aliased_observations\":"<<aliased_observations
       <<",\n  \"maximum_leaf_width\":"<<max_width
       <<",\n  \"exact_keys\":"<<exact_ranges.size()
       <<",\n  \"exact_conflicts\":"<<exact_conflicts
       <<",\n  \"exact_conflict_observations\":"<<exact_conflict_observations
       <<",\n  \"exact_reused_keys\":"<<exact_reused_keys
       <<",\n  \"exact_reused_observations\":"<<exact_reused_observations
       <<",\n  \"exact_maximum_leaf_width\":"<<exact_max_width
       <<",\n  \"hash_collisions\":"<<hash_collisions<<"\n}\n";
    std::cout<<"Shadow oracle diagnostic complete\n"
             <<"injected_action="<<injected_action
             <<" flux_events="<<total_events
             <<" observations="<<total_observations
             <<" unique_keys="<<ranges.size()
             <<" conflicting_keys="<<conflicting_keys
             <<" aliased_observations="<<aliased_observations
             <<" max_width="<<max_width<<"\n"
             <<"exact_keys="<<exact_ranges.size()
             <<" exact_conflicts="<<exact_conflicts
             <<" exact_conflict_observations="<<exact_conflict_observations
             <<" exact_reused_keys="<<exact_reused_keys
             <<" exact_reused_observations="<<exact_reused_observations
             <<" exact_max_width="<<exact_max_width
             <<" hash_collisions="<<hash_collisions<<"\n"
             <<"output="<<output.string()<<"\n";
    return conflicting_keys==0?0:1;
}
