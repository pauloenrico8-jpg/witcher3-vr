#pragma once
#include "modern_camera_history.h"
#include "modern_camera_history_math.h"
namespace w3vr::camera_history {
// Build and attach FOUR records to OWN descriptor buffers atomically. This
// never constructs or clones Native objects and never modifies Game storage.
inline bool materialize(const Plan& plan,const temporal_source::Observation& source,
    const engine_camera::TemporalRecord& reference,const engine_camera::TemporalRecord& canonical_invalid,
    modern_camera_pose::PreparedPair& pair) {
    const auto* c=plan.inputs.identity.contract;const auto* layout=engine_camera_layout::selected(c);
    factory_clock::Values values{};values.ticks=plan.clock.ticks;values.scale=plan.clock.scale;
    factory_clock::Sample converted{};
    if(!plan.ticket || !accepted(plan.inputs) || !layout || canonical_invalid[0]!=0 ||
        static_cast<unsigned>(plan.reset)>static_cast<unsigned>(ResetReason::session) ||
        !factory_clock::sample_from(values,converted) || !temporal_source::same_float(converted.time,plan.clock.time) ||
        !modern_camera_pose::prepared_for(pair,c,plan.inputs.identity.display_time) ||
        source.clock.ticks!=plan.clock.ticks || source.clock.scale!=plan.clock.scale ||
        !temporal_source::same_float(source.clock.time,plan.clock.time))return false;
    if(source.kind==temporal_source::Kind::built){
        if(!camera_history_math::matches_reference(c,source.previous,source.clock.time,reference))return false;
    }else if(source.kind!=temporal_source::Kind::reset || plan.reset==ResetReason::none ||
        !temporal_source::record_equal(source.reset_record,canonical_invalid))return false;
    modern_camera_pose::PreparedPair staged;
    try {staged=pair;}catch(const std::bad_alloc&){return false;}
    for(unsigned eye=0;eye<2;++eye)for(unsigned camera=0;camera<2;++camera){
        auto span=std::span<std::uint8_t>(staged.descriptors[eye]).subspan(layout->descriptor_cameras[camera],layout->camera_bytes);
        if(std::memcmp(span.data(),plan.inputs.current[eye][camera].data(),engine_camera::source_prefix_bytes) ||
            std::memcmp(span.data()+0x520,plan.inputs.center[eye][camera].data(),8))return false;
        engine_camera::TemporalRecord record{};
        if(plan.reset!=ResetReason::none)record=canonical_invalid;
        else{
            engine_camera::TemporalRecord symmetric{};
            if(!camera_history_math::build(c,plan.previous[eye][camera],plan.clock.time,symmetric) ||
                !camera_history_projection::center_fresh_record(c,plan.inputs.center[eye][camera],symmetric,record))return false;
        }
        if(!engine_camera::write_previous_record(span,*c,record))return false;
    }
    pair=std::move(staged);return true;
}
} // namespace w3vr::camera_history
