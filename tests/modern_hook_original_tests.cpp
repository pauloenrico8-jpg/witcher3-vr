#include "modern_hook_original.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <type_traits>
namespace route=w3vr::modern_hook_original;
unsigned checks{};
void require(bool ok,const char* reason){++checks;if(!ok){std::printf("FAIL %s\n",reason);std::exit(1);}}
int targets[4],originals[4],other_targets[4],other_originals[4];
static_assert(!std::is_copy_constructible_v<route::Route>);
static_assert(!std::is_move_constructible_v<route::Route>);
int main(){
    route::Route legacy,native;
    require(!route::select(&targets[0],legacy,native),"empty route chose original");
    require(!legacy.bind(nullptr,&originals[0]) && !legacy.bind(&targets[0],nullptr) &&
        !legacy.bind(&targets[0],&targets[0]),"incomplete or direct target bound as trampoline");
    require(legacy.bind(&targets[0],&originals[0]),"valid intermediary original rejected");
    require(native.bind(&other_targets[0],&other_originals[0]),"independent native original rejected");
    require(route::select(&targets[0],legacy,native)==&originals[0],"wrapper receiver got native original");
    require(route::select(&other_targets[0],legacy,native)==&other_originals[0],"native receiver got wrapper original");
    require(!route::select(&targets[1],legacy,native) && !route::select(nullptr,legacy,native),"unknown receiver accepted");
    require(legacy.bind(&targets[0],&originals[0]),"same association was not idempotent");
    route::Route reused,ambiguous;
    require(reused.bind(&targets[0],&originals[0]) && route::select(&targets[0],legacy,reused)==&originals[0],"same exact owner could not be shared");
    require(ambiguous.bind(&targets[0],&other_originals[0]) && !route::select(&targets[0],legacy,ambiguous),"two different originals for same target chose by priority");
    route::Route sealed;
    require(sealed.bind(&targets[2],&originals[2]) && !sealed.bind(&targets[3],&originals[3]) &&
        !sealed.for_target(&targets[2]) && !sealed.for_target(&targets[3]) && !sealed.bind(&targets[2],&originals[2]),
        "changed target recovered or retained direct-call permission");
    require(!legacy.for_target(&other_targets[0]),"native endpoint inherited proxy association");
    // A single serialized installer publishes an immutable pair; readers can
    // see absent or the exact pair, never an incomplete/retargeted association.
    route::Route concurrent;std::atomic<bool> start{},bad{},done{};std::atomic<unsigned> reads{};
    std::thread reader([&]{start.store(true);do{
        if(auto* found=concurrent.for_target(&targets[0]);found && found!=&originals[0])bad.store(true);
        if(concurrent.for_target(&targets[1]))bad.store(true);reads.fetch_add(1);
    }while(!done.load());});
    while(!start.load())std::this_thread::yield();
    require(concurrent.bind(&targets[0],&originals[0]),"concurrent initial association rejected");
    for(unsigned i=0;i<100;++i)require(concurrent.bind(&targets[0],&originals[0]),"same immutable pair changed");
    done.store(true);reader.join();require(!bad.load() && reads.load()>0,"reader observed partial/foreign original");
    require(!route::wrapper_passthrough(false,false) && !route::wrapper_passthrough(false,true) &&
        route::wrapper_passthrough(true,false) && !route::wrapper_passthrough(true,true),"wrapper observation policy wrong");
    // Forwarding-spy scenario: a modern wrapper forwards its unmodified array
    // exactly once; only its deeper native endpoint observes. Outside modern
    // mode the legacy endpoint still owns its existing observation.
    const void* array[]{&targets[0],&targets[1]};unsigned wrapper_calls{},native_calls{},wrapper_observations{},native_observations{};
    const void* const* received{};unsigned received_count{};
    auto native_call=[&](unsigned count,const void* const* values){++native_calls;
        if(!route::wrapper_passthrough(true,true))++native_observations;received=values;received_count=count;return -17;};
    auto wrapper_call=[&](bool modern,unsigned count,const void* const* values){++wrapper_calls;
        if(!route::wrapper_passthrough(modern,false))++wrapper_observations;return native_call(count,values);};
    require(wrapper_call(true,2,array)==-17 && wrapper_calls==1 && native_calls==1 && wrapper_observations==0 &&
        native_observations==1 && received==array && received_count==2 && received[0]==array[0] && received[1]==array[1],
        "nested wrapper duplicated observation/Original or changed array/return");
    require(wrapper_call(false,0,nullptr)==-17 && wrapper_calls==2 && native_calls==2 && wrapper_observations==1 &&
        received==nullptr && received_count==0,"legacy observation or empty Original arguments changed");
    std::printf("PASS %u original-route CPU checks; wrapper/native objects and return are spies, no SDK/Game/GPU\n",checks);
}
