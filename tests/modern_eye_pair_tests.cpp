#include "modern_eye_pair.h"
#include <cstdlib>
#include <iostream>
#include <vector>
namespace pair=w3vr::modern_eye_pair;
namespace core=w3vr::render_core;
namespace cam=w3vr::engine_camera;
int checks{};
void require(bool value,const char* why){++checks;if(!value){std::cerr<<why<<'\n';std::exit(1);}}
pair::Pair fixture(const core::Profile* profile,std::uint32_t generation=9,std::uint64_t id=100,std::uint64_t serial=50){
    pair::Pair p{};
    for(int eye=0;eye<2;++eye){auto& o=p[eye];
        o.identity={profile,0x1000,{static_cast<std::uintptr_t>(0x2000+eye*0x100),generation,id,eye,true,true}};
        o.texture=0x3000+eye*0x100;o.command=0x4000;o.producer=0x5000;o.fence=0x6000;o.consumer=0x7000;
        o.recording_epoch=8;o.submitted_epoch=8;o.serial=serial+eye;o.fence_value=20;
        o.source_eye=eye;o.source_eye_valid=true;o.view_valid=true;o.initialized=true;o.ordered=true;
    }return p;
}
const cam::TemporalContract* contract(const core::Profile* profile){
    return profile==&core::remastered_500c ? &cam::remastered_500c:&cam::remastered_1048522;
}
bool accepts(const pair::Pair& p,const core::Profile* profile,std::uint64_t last=0){
    return pair::accepts(p,contract(profile),profile,true,9,0x7000,last);
}
const pair::Route strict{true,true,true,false,false,false};
void profiles_and_receipts(const core::Profile* profile){
    auto good=fixture(profile);const auto* foreign=profile==&core::remastered_500c ? &core::remastered_1048522:&core::remastered_500c;
    const auto copied=*profile;const auto copied_contract=*contract(profile);
    require(accepts(good,profile),"Two copies on one real recording were rejected");
    auto separate=good;separate[1].command=0x4900;separate[1].producer=0x5900;separate[1].fence=0x6900;
    require(accepts(separate,profile),"Independently ordered producer queues rejected");
    require(!pair::accepts(good,&copied_contract,profile,true,9,0x7000,0) &&
        !pair::accepts(good,nullptr,profile,true,9,0x7000,0) &&
        !pair::accepts(good,contract(profile),&copied,true,9,0x7000,0) &&
        !pair::accepts(good,contract(profile),foreign,true,9,0x7000,0) &&
        !pair::accepts(good,&cam::legacy_404,profile,true,9,0x7000,0),"Unknown/copied/mixed installation adopted output");
    require(!pair::accepts(good,contract(profile),profile,false,9,0x7000,0) &&
        !pair::accepts(good,contract(profile),profile,true,10,0x7000,0) &&
        !pair::accepts(good,contract(profile),profile,true,9,0,0) &&
        !pair::accepts(good,contract(profile),profile,true,9,0x7100,0),"Closed/current queue or generation mismatch accepted");
    require(!accepts(good,profile,100) && !accepts(good,profile,101),"Old/equal pair promoted");
    for(int eye=0;eye<2;++eye)for(int fault=0;fault<31;++fault){
        auto p=good;auto& o=p[eye];
        switch(fault){
        case 0:o.identity.profile=nullptr;break;case 1:o.identity.profile=foreign;break;
        case 2:o.identity.profile=&copied;break;case 3:o.identity.renderer=0;break;
        case 4:o.identity.renderer+=0x100;break;case 5:o.identity.frame.frame=0;break;
        case 6:o.identity.frame.generation=8;break;case 7:o.identity.frame.pair=99;break;
        case 8:o.identity.frame.pair=0;break;case 9:o.identity.frame.pair=UINT64_MAX;break;
        case 10:o.identity.frame.eye=1-eye;break;case 11:o.identity.frame.eye=-1;break;
        case 12:o.identity.frame.view_valid=false;break;case 13:o.identity.frame.normal_factory_lineage=false;break;
        case 14:o.texture=0;break;case 15:o.command=0;break;case 16:o.producer=0;break;
        case 17:o.fence=0;break;case 18:o.consumer=0x7100;break;case 19:o.recording_epoch=0;break;
        case 20:o.recording_epoch=UINT64_MAX;break;case 21:o.submitted_epoch=9;break;
        case 22:o.serial=0;break;case 23:o.serial=UINT64_MAX;break;case 24:o.fence_value=0;break;
        case 25:o.fence_value=UINT64_MAX;break;case 26:o.source_eye=1-eye;break;
        case 27:o.source_eye_valid=false;break;case 28:o.view_valid=false;break;
        case 29:o.initialized=false;break;case 30:o.recording_pending=true;break;
        }
        require(!accepts(p,profile),"Incomplete/foreign eye, recording or timeline accepted");
        require(!pair::same(p,good),"Immutable receipt mutation not detected");
    }
    auto p=good;p[1].identity.frame.frame=p[0].identity.frame.frame;require(!accepts(p,profile),"Same Native frame relabelled as two eyes");
    p=good;p[1].texture=p[0].texture;require(!accepts(p,profile),"One owned texture relabelled as two outputs");
    p=good;p[1].serial=p[0].serial;require(!accepts(p,profile),"One copy serial relabelled as peer");
    p=good;p[0].ordered=false;require(!accepts(p,profile),"Recording mistaken for ordered submission");
    require(pair::same(good,fixture(profile)),"Matching immutable receipts differ");
}
void consume_once_and_failure(const core::Profile* profile){
    auto p=fixture(profile);pair::Consumption consume;pair::Claim claim{},sentinel{p,777};
    auto out=sentinel;
    require(!consume.begin(p,contract(profile),profile,false,9,0x7000,strict,out) && out.nonce==777,
        "Rejected call acquired claim or changed caller storage");
    for(int fault=0;fault<6;++fault){auto route=strict;
        if(fault==0)route.mode3=false;if(fault==1)route.noaa=false;if(fault==2)route.dual=false;
        if(fault==3)route.aer=true;if(fault==4)route.panel=true;if(fault==5)route.delayed_scene=true;
        require(!consume.begin(p,contract(profile),profile,true,9,0x7000,route,out),"Alternative presenter adopted strict receipt");
    }
    require(consume.begin(p,contract(profile),profile,true,9,0x7000,strict,claim),"Fresh pair not claimed");
    require(consume.current(claim,contract(profile),profile,true,9,0x7000),"Current claim rejected");
    require(!consume.begin(fixture(profile,9,101,52),contract(profile),profile,true,9,0x7000,strict,out),
        "Nested or concurrent consumer stole active pair");
    auto altered=claim;altered.outputs[0].serial++;
    require(!consume.finish(altered,true,true) && !consume.current(altered,contract(profile),profile,true,9,0x7000),
        "Changed claim completed or acquired current authority");
    auto stale=claim;stale.nonce++;
    require(!consume.finish(stale,true,true),"Foreign nonce completed active pair");
    require(!consume.current(claim,contract(profile),profile,false,9,0x7000) &&
        !consume.current(claim,contract(profile),profile,true,10,0x7000),"Readiness or generation change retained draw admission");
    require(!consume.finish(claim,true,false),"Failed xrEndFrame reported successful submission");
    require(!consume.begin(p,contract(profile),profile,true,9,0x7000,strict,out),"Failed XR attempt replayed same pair as new");
    auto next=fixture(profile,9,101,52);
    require(consume.begin(next,contract(profile),profile,true,9,0x7000,strict,out),"Independent new pair rejected after failure");
    require(consume.finish(out,true,true) && !consume.finish(out,true,true),"Successful claim finished twice");
    auto relabelled=fixture(profile,9,102,52);
    require(!consume.begin(relabelled,contract(profile),profile,true,9,0x7000,strict,out),
        "New pair label concealed repeated copy serials");
    auto new_generation=fixture(profile,10,1,54);
    require(consume.begin(new_generation,contract(profile),profile,true,10,0x7000,strict,out),
        "New generation could not start at its native pair counter");
    require(!consume.finish(out,false,true),"Unsubmitted draw reported success");
    require(!consume.begin(new_generation,contract(profile),profile,true,10,0x7000,strict,claim),
        "CPU-return or unsubmitted failure allowed replay");
    require(!consume.current(claim,contract(profile),profile,true,10,0x7000),"Old claim adopted new context");
}
void retirement(){
    for(const auto phase:{pair::UsePhase::Recording,pair::UsePhase::Executing,pair::UsePhase::Pending})
    for(bool active:{false,true})for(auto target:{0ULL,1ULL,9ULL,UINT64_MAX})
    for(auto completed:{0ULL,1ULL,8ULL,9ULL,10ULL,UINT64_MAX}){
        const bool expected=!active && phase==pair::UsePhase::Pending && target!=0 && target!=UINT64_MAX &&
            completed!=UINT64_MAX && completed>=target;
        require(pair::retired(phase,target,completed,active)==expected,
            "CPU-active, recording, failed-signal or device-lost use retired incorrectly");
    }
}
int main(){
    for(const auto* profile:{&core::remastered_500c,&core::remastered_1048522}){
        profiles_and_receipts(profile);consume_once_and_failure(profile);
    }
    retirement();
    std::cout<<checks<<" CPU checks: two version profiles, concrete output receipts, one-use presentation and retirement predicates. "
        "No Native game calls, MinHook, D3D12 GPU work, OpenXR runtime or Quest executed.\n";
}
