#pragma once
#include "modern_eye_capture.h"
#include <array>
#include <tuple>

namespace w3vr::modern_eye_pair {
// Copied submission receipts. None of these numeric keys owns a Native or COM
// object, proves completed pixels, or authorizes executable compatibility.
struct Output {
    modern_eye_capture::Identity identity{};
    std::uintptr_t texture{}, command{}, producer{}, fence{}, consumer{};
    std::uint64_t recording_epoch{}, submitted_epoch{}, serial{}, fence_value{};
    std::uint32_t source_eye{UINT32_MAX};
    bool source_eye_valid{}, view_valid{}, initialized{}, recording_pending{}, ordered{};
    std::uint64_t binding_epoch{}; // Exact owned graphics snapshot used by capture.
};
using Pair=std::array<Output,2>;
inline bool finite(std::uint64_t value){return value!=0 && value!=UINT64_MAX;}
inline bool known(const render_core::Profile* profile){
    return profile==&render_core::remastered_500c || profile==&render_core::remastered_1048522;
}
inline bool same(const Output& a,const Output& b){
    const auto fields=[](const Output& o){return std::tuple{
        o.identity.profile,o.identity.renderer,o.identity.frame.frame,o.identity.frame.generation,
        o.identity.frame.pair,o.identity.frame.eye,o.identity.frame.view_valid,
        o.identity.frame.normal_factory_lineage,o.identity.frame.display_time,o.identity.frame.xr,o.texture,o.command,o.producer,o.fence,o.consumer,
        o.recording_epoch,o.submitted_epoch,o.serial,o.fence_value,o.source_eye,o.source_eye_valid,
        o.view_valid,o.initialized,o.recording_pending,o.ordered,o.binding_epoch};};
    return fields(a)==fields(b);
}
inline bool same(const Pair& a,const Pair& b){return same(a[0],b[0]) && same(a[1],b[1]);}
inline bool accepts(const Pair& outputs,const engine_camera::TemporalContract* contract,
    const render_core::Profile* installed,bool ready,std::uint32_t generation,
    std::uintptr_t consumer,std::uint64_t last_pair,std::uint64_t binding_epoch){
    if(!consumer || !finite(binding_epoch) || !render_core::ready_for(contract,installed,ready))return false;
    for(std::uint32_t eye=0;eye<2;++eye){
        const auto& o=outputs[eye];
        if(!modern_eye_capture::admits(o.identity,contract,installed,ready,generation) ||
            o.identity.frame.display_time<=0 || o.identity.frame.display_time==INT64_MAX ||
            o.identity.frame.eye!=static_cast<int>(eye) || o.source_eye!=eye || !o.source_eye_valid ||
            !o.view_valid || !o.texture || !o.command || !o.producer || !o.fence || o.consumer!=consumer ||
            !finite(o.recording_epoch) || o.recording_epoch!=o.submitted_epoch || !finite(o.serial) ||
            o.binding_epoch!=binding_epoch || !finite(o.fence_value) || !o.initialized || o.recording_pending || !o.ordered)return false;
    }
    return outputs[0].identity.renderer==outputs[1].identity.renderer &&
        outputs[0].identity.frame.frame!=outputs[1].identity.frame.frame &&
        outputs[0].identity.frame.pair==outputs[1].identity.frame.pair &&
        outputs[0].identity.frame.xr==outputs[1].identity.frame.xr &&
        outputs[0].identity.frame.display_time==outputs[1].identity.frame.display_time &&
        outputs[0].identity.frame.pair>last_pair && outputs[0].texture!=outputs[1].texture &&
        outputs[0].serial!=outputs[1].serial;
    // Two different lists are NOT required: one real recording can contain two
    // ordered copies. Their individual serials/physical eyes remain distinct.
}
// The caller supplies the actual xrWaitFrame/xrBeginFrame target captured
// for publication. Do not synthesize it from clock/Present cadence.
struct Target {std::int64_t display_time{};bool prepared{};xr_frame::Identity xr{};};
inline bool matches_target(const Pair& outputs,Target target) {
    return xr_frame::valid(target.xr) && target.prepared && target.display_time>0 && target.display_time!=INT64_MAX &&
        outputs[0].identity.frame.display_time==target.display_time &&
        outputs[1].identity.frame.display_time==target.display_time &&
        outputs[0].identity.frame.xr==target.xr && outputs[1].identity.frame.xr==target.xr;
}
struct Route {
    bool mode3{}, noaa{}, dual{}, aer{}, panel{}, delayed_scene{};
};
inline bool strict_scene(const Route& route){
    return route.mode3 && route.noaa && route.dual && !route.aer && !route.panel && !route.delayed_scene;
}
struct Claim {Pair outputs{};std::uint64_t nonce{};};
class Consumption {
    const render_core::Profile* profile_{};
    std::uint32_t generation_{};
    std::uint64_t last_attempt_{},nonce_{};
    std::array<std::uint64_t,2> last_serials_{};
    Claim active_{};
    xr_frame::Identity attempted_xr_{};
public:
    // Host serializes this helper with its immutable output snapshot. A failed
    // XR attempt still burns that pair; it cannot become a new application
    // image through retries, rescue cadence or a readiness toggle.
    bool begin(const Pair& outputs,const engine_camera::TemporalContract* contract,
        const render_core::Profile* installed,bool ready,std::uint32_t generation,
        std::uintptr_t consumer,std::uint64_t binding_epoch,const Route& route,Claim& claim){
        if(!strict_scene(route) || !accepts(outputs,contract,installed,ready,generation,consumer,0,binding_epoch) ||
            active_.nonce || nonce_==UINT64_MAX)return false;
        const auto xr=outputs[0].identity.frame.xr;
        if(xr_frame::valid(attempted_xr_) && !xr_frame::newer(xr,attempted_xr_))return false;
        const bool new_context=profile_!=installed || generation_!=generation || xr.epoch!=attempted_xr_.epoch;
        if((!new_context && outputs[0].identity.frame.pair<=last_attempt_) ||
            outputs[0].serial<=last_serials_[0] || outputs[1].serial<=last_serials_[1])return false;
        last_serials_={outputs[0].serial,outputs[1].serial};attempted_xr_=xr;
        profile_=installed;generation_=generation;last_attempt_=outputs[0].identity.frame.pair;
        active_={outputs,++nonce_};claim=active_;return true;
    }
    bool begin_for_frame(const Pair& outputs,const engine_camera::TemporalContract* contract,
        const render_core::Profile* installed,bool ready,std::uint32_t generation,
        std::uintptr_t consumer,std::uint64_t binding_epoch,const Route& route,Target target,Claim& claim){
        return matches_target(outputs,target) && begin(outputs,contract,installed,ready,generation,
            consumer,binding_epoch,route,claim);
    }
    bool current(const Claim& claim,const engine_camera::TemporalContract* contract,
        const render_core::Profile* installed,bool ready,std::uint32_t generation,std::uintptr_t consumer,std::uint64_t binding_epoch)const{
        return claim.nonce!=0 && claim.nonce==active_.nonce && same(claim.outputs,active_.outputs) &&
            installed==profile_ && generation==generation_ &&
            accepts(claim.outputs,contract,installed,ready,generation,consumer,0,binding_epoch);
    }
    bool finish(const Claim& claim,bool submitted,bool xr_succeeded){
        if(!claim.nonce || claim.nonce!=active_.nonce || !same(claim.outputs,active_.outputs))return false;
        active_={};return submitted && xr_succeeded;
    }
};
enum class UsePhase {Recording,Executing,Pending};
// Only a known returned GPU signal can retire submitted use. Device-removal
// UINT64_MAX and unknown/failed signals never authorize releasing references.
inline bool retired(UsePhase phase,std::uint64_t target,std::uint64_t completed,bool cpu_active){
    return !cpu_active && phase==UsePhase::Pending && finite(target) && completed!=UINT64_MAX && completed>=target;
}
} // namespace w3vr::modern_eye_pair
