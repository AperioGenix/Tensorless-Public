#include "tensorless/tensorless.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>

namespace {
constexpr uint32_t kBlock=3;
constexpr uint32_t kTrainingStates=16000;
constexpr uint32_t kValidationStates=4000;
constexpr uint8_t kMinSeedBucket=15;
constexpr uint8_t kMaxSeedBucket=24;
constexpr uint8_t kSeedBuckets[11]{0,15,16,17,18,19,20,21,22,23,24};

struct Key {
    uint8_t phase,face,bulk_bucket,incoming_bucket;
    bool operator<(const Key& r)const {
        if(phase!=r.phase)return phase<r.phase;
        if(face!=r.face)return face<r.face;
        if(bulk_bucket!=r.bulk_bucket)return bulk_bucket<r.bulk_bucket;
        return incoming_bucket<r.incoming_bucket;
    }
};
struct FaceKey {
    uint8_t phase,face;
    bool operator<(const FaceKey&r)const{return phase!=r.phase?phase<r.phase:face<r.face;}
};
struct Range {
    uint64_t minimum=std::numeric_limits<uint64_t>::max(),maximum=0,count=0;
    void Add(uint64_t v){if(v<minimum)minimum=v;if(v>maximum)maximum=v;++count;}
    uint64_t Midpoint()const{return minimum+(maximum-minimum)/2ULL;}
};
enum class SampleResult { Valid, Rejected, Error };

uint32_t Next(uint32_t&s){s=s*1664525U+1013904223U;return s;}
uint8_t LogBucket(uint64_t v){uint8_t b=0;while(v>1){v>>=1;++b;}return b;}
uint64_t PhaseIndex(uint32_t p,uint32_t f){return (((uint64_t(p)*6+f)*3)*3+1);}

SampleResult MakeSample(uint32_t attempt,uint8_t seed_bucket,TensorlessHandle** micro,
                        TensorlessDaemon& macro_state,uint64_t outcomes[kBlock][6]) {
    TensorlessCreateInfo info{kBlock,kBlock,kBlock,100000000ULL};
    if(Tensorless_Create(&info,micro)!=TENSORLESS_STATUS_OK)return SampleResult::Error;
    const auto finish=[&](SampleResult result) {
        Tensorless_Destroy(*micro);
        *micro=nullptr;
        return result;
    };
    TensorlessFpmConstants constants{};
    Tensorless_GetFpmConstants(&constants);
    uint32_t rng=0x85ebca6bU^attempt;
    const uint32_t bucket_floor=1U<<seed_bucket;
    const uint32_t seeded_load=seed_bucket==0
        ? Next(rng)%32768U
        : bucket_floor+(Next(rng)%bucket_floor);
    for(uint64_t index=0;index<Tensorless_DaemonCount(*micro);++index) {
        TensorlessDaemon daemon{};
        if(Tensorless_GetDaemon(*micro,index,&daemon)!=TENSORLESS_STATUS_OK)
            return finish(SampleResult::Error);
        daemon.E=90000000ULL+(Next(rng)%10000001U);
        for(uint32_t ch=0;ch<TENSORLESS_FPM_CHANNELS;++ch)
            daemon.psi[ch]=int32_t(Next(rng)%7U)-3;
        if(index==13) {
            const uint64_t forward=uint64_t(daemon.R[0][1])+seeded_load;
            if(forward>std::numeric_limits<uint32_t>::max())
                return finish(SampleResult::Rejected);
            daemon.R[0][1]=static_cast<uint32_t>(forward);
            daemon.R[0][0]+=(seeded_load*6ULL)/100ULL;
            daemon.R[1][1]+=(seeded_load*6ULL)/100ULL;
            daemon.R[2][2]+=(seeded_load*3ULL)/100ULL;
        }
        uint64_t action=0;
        Tensorless_GetRouteAction(&daemon,&action);
        if(action>constants.l_max)return finish(SampleResult::Rejected);
        if(Tensorless_SetDaemon(*micro,index,&daemon)!=TENSORLESS_STATUS_OK)
            return finish(SampleResult::Error);
    }
    if(Tensorless_EnableFluxRecording(*micro,kBlock)!=TENSORLESS_STATUS_OK)
        return finish(SampleResult::Error);
    TensorlessHandle* macro=nullptr;
    if(Tensorless_CoarseGrain(*micro,kBlock,&macro)!=TENSORLESS_STATUS_OK)
        return finish(SampleResult::Error);
    if(Tensorless_GetDaemon(macro,0,&macro_state)!=TENSORLESS_STATUS_OK) {
        Tensorless_Destroy(macro);
        return finish(SampleResult::Error);
    }
    Tensorless_Destroy(macro);
    TensorlessFpmStats stats{};
    if(Tensorless_Run(*micro,uint64_t(kBlock)*27ULL,&stats)!=TENSORLESS_STATUS_OK)
        return finish(SampleResult::Error);
    uint64_t phase_flux[kBlock*6*3*3]{};
    if(Tensorless_CopyPhaseFlux(*micro,phase_flux,kBlock*6*3*3)!=TENSORLESS_STATUS_OK)
        return finish(SampleResult::Error);
    for(uint32_t phase=0;phase<kBlock;++phase)for(uint32_t face=0;face<6;++face)
        outcomes[phase][face]=phase_flux[PhaseIndex(phase,face)];
    return SampleResult::Valid;
}

Key MakeKey(const TensorlessDaemon&m,uint32_t p,uint32_t f){
    return{static_cast<uint8_t>(p),static_cast<uint8_t>(f),LogBucket(m.R_bulk[0][1]),
           LogBucket(m.R_boundary_in[f][0][1])};
}
}

int main(int argc,char**argv){
    const std::filesystem::path output=argc>1?argv[1]:"core/artifacts/rg_flow_b3.json";
    std::map<Key,Range> tree;
    std::map<FaceKey,Range> fallback;
    uint64_t attempted[25]{},accepted[25]{},rejected[25]{};
    uint32_t attempt=0,valid=0;
    while(valid<kTrainingStates){
        const uint8_t bucket=kSeedBuckets[attempt%11U];
        ++attempted[bucket];
        TensorlessHandle*micro=nullptr;TensorlessDaemon macro{};uint64_t outcomes[kBlock][6]{};
        const SampleResult result=MakeSample(attempt,bucket,&micro,macro,outcomes);
        ++attempt;
        if(result==SampleResult::Error){std::cerr<<"Training generation failed\n";return 2;}
        if(result==SampleResult::Rejected){++rejected[bucket];Tensorless_Destroy(micro);continue;}
        ++accepted[bucket];++valid;
        for(uint32_t p=0;p<kBlock;++p)for(uint32_t f=0;f<6;++f){
            tree[MakeKey(macro,p,f)].Add(outcomes[p][f]);
            fallback[{static_cast<uint8_t>(p),static_cast<uint8_t>(f)}].Add(outcomes[p][f]);
        }
        Tensorless_Destroy(micro);
    }

    uint64_t error_sum=0,max_error=0,covered=0,exact=0,records=0;
    valid=0;
    while(valid<kValidationStates){
        const uint8_t bucket=kSeedBuckets[attempt%11U];
        ++attempted[bucket];
        TensorlessHandle*micro=nullptr;TensorlessDaemon macro{};uint64_t outcomes[kBlock][6]{};
        const SampleResult result=MakeSample(attempt,bucket,&micro,macro,outcomes);
        ++attempt;
        if(result==SampleResult::Error){std::cerr<<"Validation generation failed\n";return 3;}
        if(result==SampleResult::Rejected){++rejected[bucket];Tensorless_Destroy(micro);continue;}
        ++accepted[bucket];++valid;
        for(uint32_t p=0;p<kBlock;++p)for(uint32_t f=0;f<6;++f){
            const auto found=tree.find(MakeKey(macro,p,f));
            const Range*range=nullptr;
            if(found!=tree.end()){range=&found->second;++exact;}
            else range=&fallback[{static_cast<uint8_t>(p),static_cast<uint8_t>(f)}];
            const uint64_t observed=outcomes[p][f],predicted=range->Midpoint();
            const uint64_t error=observed>predicted?observed-predicted:predicted-observed;
            error_sum+=error;if(error>max_error)max_error=error;
            if(observed>=range->minimum&&observed<=range->maximum)++covered;
            ++records;
        }
        Tensorless_Destroy(micro);
    }

    if(!output.parent_path().empty())std::filesystem::create_directories(output.parent_path());
    std::ofstream out(output);if(!out){std::cerr<<"Cannot write output\n";return 4;}
    out<<"{\n  \"schema\":\"tensorless-conditional-rg-v2\",\n"
       <<"  \"block_width\":3,\n  \"training_states\":"<<kTrainingStates
       <<",\n  \"validation_states\":"<<kValidationStates
       <<",\n  \"tree_leaves\":"<<tree.size()
       <<",\n  \"validation_records\":"<<records
       <<",\n  \"exact_leaf_records\":"<<exact
       <<",\n  \"interval_covered_records\":"<<covered
       <<",\n  \"mae\":"<<(error_sum/records)
       <<",\n  \"maximum_absolute_deviation\":"<<max_error<<",\n";
    out<<"  \"seed_bucket_coverage\":[";
    for(uint32_t index=0;index<11;++index) {
        const uint8_t b=kSeedBuckets[index];
        out<<(index==0?"":",")<<"{\"bucket\":"<<uint32_t(b)
           <<",\"attempted\":"<<attempted[b]<<",\"accepted\":"<<accepted[b]
           <<",\"rejected\":"<<rejected[b]<<"}";
    }
    out<<"],\n  \"leaves\":[";
    bool first=true;
    for(const auto&[key,range]:tree){
        out<<(first?"":",")<<"{\"phase\":"<<uint32_t(key.phase)
           <<",\"face\":"<<uint32_t(key.face)
           <<",\"bulk_bucket\":"<<uint32_t(key.bulk_bucket)
           <<",\"incoming_bucket\":"<<uint32_t(key.incoming_bucket)
           <<",\"count\":"<<range.count<<",\"minimum\":"<<range.minimum
           <<",\"maximum\":"<<range.maximum<<",\"midpoint\":"<<range.Midpoint()<<"}";
        first=false;
    }
    out<<"]\n}\n";
    std::cout<<"Strong-field RG flow evaluation complete\n"
             <<"training="<<kTrainingStates<<" validation="<<kValidationStates
             <<" leaves="<<tree.size()<<" records="<<records
             <<" exact="<<exact<<" covered="<<covered
             <<" mae="<<(error_sum/records)<<" max_deviation="<<max_error<<"\n";
    for(uint8_t b:kSeedBuckets)
        std::cout<<"bucket="<<uint32_t(b)<<" attempted="<<attempted[b]
                 <<" accepted="<<accepted[b]<<" rejected="<<rejected[b]<<"\n";
    std::cout<<"output="<<output.string()<<"\n";
    return 0;
}
