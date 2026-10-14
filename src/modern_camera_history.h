#pragma once
#include "modern_camera_pose.h"
#include "xr_frame_identity.h"
#include "modern_temporal_source.h"
#include "modern_camera_history_projection.h"
#include <bit>
#include <limits>

namespace w3vr::camera_history {
using Prefix=temporal_source::Prefix;
using FourPrefixes=std::array<std::array<Prefix,2>,2>; // [HMD eye][internal camera]
using FourCenters=std::array<std::array<std::array<float,2>,2>,2>;
using FourRecords=std::array<std::array<engine_camera::TemporalRecord,2>,2>;
struct Identity {
    const engine_camera::TemporalContract* contract{}; // Static singleton, not a Game object.
    std::uint64_t generation{},pair{};
    std::int64_t display_time{};
    xr_frame::Identity xr{};
    bool operator==(const Identity&) const=default;
};
inline bool accepted(const Identity& i) noexcept {
    return xr_frame::valid(i.xr) && i.contract==&engine_camera::remastered_1048522 && i.generation && i.generation!=UINT64_MAX &&
        i.pair && i.pair!=UINT64_MAX && i.display_time>0;
}
struct Inputs {
    Identity identity{};
    FourPrefixes current{};
    FourCenters center{};
    Prefix raw_primary{}; // Original native current prefix, never a pointer.
    std::array<std::uint32_t,9> rig{}; // Recenter pose, translation scale, pitch option.
    bool operator==(const Inputs&) const=default;
};
inline bool valid_prefix(const Prefix& p) noexcept {
    engine_camera::TemporalInputs unused{};
    return engine_camera::read_temporal_inputs(p,0,unused); // Parse only, not a native time.
}
inline bool accepted(const Inputs& i) noexcept {
    if(!accepted(i.identity) || !valid_prefix(i.raw_primary))return false;
    for(std::size_t eye=0;eye<2;++eye)for(std::size_t camera=0;camera<2;++camera){
        if(!valid_prefix(i.current[eye][camera]))return false;
        for(float c:i.center[eye][camera])if(!std::isfinite(c))return false;
    }
    return true;
}
// Extract all four owned value prefixes from ONE already-frozen pose pair.
// Embedded native objects/pointers in descriptors are never retained here.
inline bool snapshot(const modern_camera_pose::PreparedPair& pair,std::span<const std::uint8_t> raw,
    const modern_camera_pose::TrackingSample& tracking,modern_camera_pose::Options options,
    Identity identity,Inputs& out) noexcept {
    const auto* layout=engine_camera_layout::selected(identity.contract);
    if(!accepted(identity) || !layout || !tracking.valid || tracking.display_time!=identity.display_time ||
        !std::isfinite(options.head_translation_scale) || options.head_translation_scale<0 || options.head_translation_scale>2 ||
        !modern_camera_pose::prepared_for(pair,identity.contract,identity.display_time) ||
        !scene_descriptor::copy_accepted(scene_factory::Version::remastered_1048522,
            scene_descriptor::copy_policy(scene_factory::Version::remastered_1048522,false),raw,raw.size()))return false;
    Inputs staged{};staged.identity=identity;
    const std::array<float,8> rig{tracking.origin_heading.x,tracking.origin_heading.y,tracking.origin_heading.z,
        tracking.origin_heading.w,tracking.origin_position.x,tracking.origin_position.y,tracking.origin_position.z,
        options.head_translation_scale};
    for(std::size_t k=0;k<rig.size();++k){if(!std::isfinite(rig[k]))return false;staged.rig[k]=std::bit_cast<std::uint32_t>(rig[k]);}
    staged.rig[8]=options.lock_game_pitch?1u:0u;
    std::memcpy(staged.raw_primary.data(),raw.data()+layout->descriptor_cameras[0],staged.raw_primary.size());
    for(std::size_t eye=0;eye<2;++eye)for(std::size_t camera=0;camera<2;++camera){
        const auto offset=layout->descriptor_cameras[camera];
        std::memcpy(staged.current[eye][camera].data(),pair.descriptors[eye].data()+offset,engine_camera::source_prefix_bytes);
        std::memcpy(staged.center[eye][camera].data(),pair.descriptors[eye].data()+offset+0x520,8);
    }
    if(!accepted(staged))return false;out=staged;return true;
}
inline bool same_lens(const Prefix& a,const Prefix& b) noexcept {
    for(std::size_t offset:{0x1Cu,0x28u,0x2Cu,0x30u,0x34u})
        if(std::memcmp(a.data()+offset,b.data()+offset,4))return false;
    return true;
}
enum class ResetReason { none,native_reset,cold,generation,order,source,lens,rig,clock,session };
struct Plan {
    std::uint64_t ticket{};
    Inputs inputs{};
    FourPrefixes previous{};
    factory_clock::Sample clock{};
    ResetReason reset{ResetReason::cold};
};
// Own CPU input continuity only. A commit does NOT assert Game rendering,
// GPU completion, native object ownership or permission to call NativeBuilder.
// The host serializes this object independently of native queue/thread state.
class Bank {
    Inputs committed_{};
    factory_clock::Sample clock_{};
    Plan pending_{};
    std::uint64_t revision_{};
    Identity watermark_{};
    bool have_{},pending_valid_{},seen_{};
public:
    Bank()=default;
    Bank(const Bank&)=delete;Bank& operator=(const Bank&)=delete;
    void clear() noexcept {have_=false;pending_valid_=false;}
    bool prepare(const Inputs& input,const temporal_source::Observation& observed,Plan& out) noexcept {
        // Any entry masks the old reservation, including a rejected entry.
        pending_valid_=false;
        if(revision_==UINT64_MAX){clear();return false;}
        ++revision_;
        if(!accepted(input.identity)){clear();return false;}
        if(seen_ && (!xr_frame::newer(input.identity.xr,watermark_.xr) ||
            input.identity.generation<watermark_.generation ||
            (input.identity.xr.epoch==watermark_.xr.epoch && input.identity.generation==watermark_.generation &&
                (input.identity.pair<=watermark_.pair || input.identity.display_time<=watermark_.display_time)))){
            clear();return false;
        }
        watermark_=input.identity;seen_=true;
        factory_clock::Values v{};v.ticks=observed.clock.ticks;v.scale=observed.clock.scale;
        factory_clock::Sample converted{};
        if(!accepted(input) || (observed.kind!=temporal_source::Kind::built && observed.kind!=temporal_source::Kind::reset) ||
            !factory_clock::sample_from(v,converted) || !temporal_source::same_float(converted.time,observed.clock.time) ||
            (observed.kind==temporal_source::Kind::built && !valid_prefix(observed.previous)) ||
            (observed.kind==temporal_source::Kind::reset && observed.reset_record[0]!=0)){clear();return false;}
        Plan staged{};staged.ticket=revision_;staged.inputs=input;staged.clock=observed.clock;
        auto reason=ResetReason::none;
        if(observed.kind==temporal_source::Kind::reset)reason=ResetReason::native_reset;
        else if(!have_)reason=ResetReason::cold;
        else if(committed_.identity.xr.epoch!=input.identity.xr.epoch || committed_.identity.xr.session!=input.identity.xr.session)reason=ResetReason::session;
        else if(committed_.identity.contract!=input.identity.contract || committed_.identity.generation!=input.identity.generation)reason=ResetReason::generation;
        else if(observed.clock.ticks<clock_.ticks || observed.clock.scale!=clock_.scale)reason=ResetReason::clock;
        else if(observed.previous!=committed_.raw_primary)reason=ResetReason::source;
        else if(committed_.rig!=input.rig)reason=ResetReason::rig;
        else for(std::size_t eye=0;eye<2;++eye)for(std::size_t camera=0;camera<2;++camera)
            if(!same_lens(committed_.current[eye][camera],input.current[eye][camera]) ||
                committed_.center[eye][camera]!=input.center[eye][camera])reason=ResetReason::lens;
        staged.reset=reason;
        if(reason==ResetReason::none)staged.previous=committed_.current;
        pending_=staged;pending_valid_=true;out=staged;return true;
    }
    bool commit_cpu(const Plan& plan,const Identity& current,bool native_pair_dispatched) noexcept {
        if(!pending_valid_ || !plan.ticket || plan.ticket!=pending_.ticket)return false;
        const bool same=plan.inputs==pending_.inputs && plan.previous==pending_.previous && plan.reset==pending_.reset &&
            plan.clock.ticks==pending_.clock.ticks && plan.clock.scale==pending_.clock.scale &&
            temporal_source::same_float(plan.clock.time,pending_.clock.time);
        pending_valid_=false;
        if(!native_pair_dispatched || !same || current!=pending_.inputs.identity){have_=false;return false;}
        committed_=pending_.inputs;clock_=pending_.clock;have_=true;return true;
    }
    bool cancel(std::uint64_t ticket) noexcept {
        if(!pending_valid_ || !ticket || ticket!=pending_.ticket)return false;
        pending_valid_=false;have_=false;return true;
    }
};
// Caller supplies its OWN callback in tests. A Native callback additionally
// requires a proven construction/lifetime scope OUTSIDE this data helper.
// The current host does NOT call this helper or write per-eye history yet.
// Four transient B0 records are built at NOW; the Bank caches only INPUTs.
inline bool build_records(const Plan& plan,engine_camera::NativeBuilder builder,
    const engine_camera::TemporalRecord& canonical_invalid,FourRecords& out) {
    factory_clock::Values values{};values.ticks=plan.clock.ticks;values.scale=plan.clock.scale;
    factory_clock::Sample converted{};
    if(!plan.ticket || !accepted(plan.inputs) || !factory_clock::sample_from(values,converted) ||
        !temporal_source::same_float(converted.time,plan.clock.time) ||
        static_cast<unsigned>(plan.reset)>static_cast<unsigned>(ResetReason::session))return false;
    FourRecords staged{};
    if(plan.reset!=ResetReason::none){
        if(canonical_invalid[0]!=0)return false;
        for(auto& eye:staged)for(auto& camera:eye)camera=canonical_invalid;
    }else{
        if(!builder)return false;
        for(std::size_t eye=0;eye<2;++eye)for(std::size_t camera=0;camera<2;++camera){
            if(!engine_camera::build_record(plan.previous[eye][camera],plan.clock.time,builder,staged[eye][camera]))return false;
            float actual{};std::memcpy(&actual,staged[eye][camera].data()+4,4);
            if(staged[eye][camera][0]!=1 || !temporal_source::same_float(actual,plan.clock.time))return false;
            // The fresh result is still OWN transient storage. No live camera
            // is written and the host still does not invoke this helper.
            engine_camera::TemporalRecord centered{};
            if(!camera_history_projection::center_fresh_record(plan.inputs.identity.contract,
                plan.inputs.center[eye][camera],staged[eye][camera],centered))return false;
            staged[eye][camera]=centered;
        }
    }
    out=staged;return true; // Output publishes only after ALL FOUR results succeed.
}
} // namespace w3vr::camera_history
