#pragma once

#include "engine_camera_layout.h"
#include "engine_scene_descriptor.h"
#include "openxr_eye_geometry.h"
#include "modern_camera_projection.h"
#include <array>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace w3vr::modern_camera_pose {
// A copied OpenXR locate result and its heading-only recenter, published together.
// No native pointer, runtime handle, permission to render, or completion is held.
struct TrackingSample {
    std::array<XrView, 2> views{{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}};
    XrQuaternionf origin_heading{0, 0, 0, 1};
    XrVector3f origin_position{};
    XrTime display_time{};
    bool valid{};
};
struct Options {
    float head_translation_scale{1};
    bool lock_game_pitch{true};
};
struct PreparedPair {
    const engine_camera::TemporalContract* contract{};
    XrTime display_time{};
    // Borrowed embedded pointers remain borrowed. These are byte buffers, not
    // constructed native objects. Never invoke a game destructor on them.
    std::array<std::vector<std::uint8_t>, 2> descriptors;
};
inline bool finite(XrVector3f v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline XrVector3f engine_local(XrVector3f v) { return {v.x, -v.z, v.y}; }
inline XrVector3f add(XrVector3f a, XrVector3f b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline XrVector3f subtract(XrVector3f a, XrVector3f b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline XrVector3f scaled(XrVector3f a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline XrVector3f position(const engine_camera_layout::PoseFields& p) {
    return {p.position[0],p.position[1],p.position[2]};
}
inline bool read_pose(const engine_camera::TemporalContract* contract,
    std::span<const std::uint8_t> camera,
    engine_camera_layout::PoseFields& pose) {
    const auto* layout=engine_camera_layout::selected(contract);
    if (!engine_camera_layout::modern(layout) || camera.size()<layout->camera_bytes) return false;
    engine_camera_layout::PoseFields candidate{};
    std::memcpy(candidate.position.data(), camera.data(), sizeof(candidate.position));
    std::memcpy(candidate.rotation_degrees.data(), camera.data()+0x10, sizeof(candidate.rotation_degrees));
    for (float v : candidate.position) if (!std::isfinite(v)) return false;
    for (float v : candidate.rotation_degrees) if (!std::isfinite(v)) return false;
    float fov{};
    std::memcpy(&fov, camera.data()+0x1C, sizeof(fov));
    if (!std::isfinite(fov) || fov<=0 || fov>=180) return false;
    pose=candidate;
    return true; // Perspective only; no partial output on failure.
}
inline XrQuaternionf orientation(const engine_camera_layout::PoseFields& pose) {
    return openxr_eye_geometry::from_redengine_view_euler_degrees(
        pose.rotation_degrees[0],pose.rotation_degrees[1],pose.rotation_degrees[2]);
}
inline bool encode_orientation(XrQuaternionf q,
    const std::array<float,3>& reference, std::array<float,3>& result) {
    using namespace openxr_eye_geometry;
    XrQuaternionf n{};
    if (!normalize(q,n)) return false;
    auto e = to_redengine_view_euler_degrees(n);
    // At vertical pitch, roll/yaw are coupled. Keep the reference roll and
    // solve the coupled heading instead of emitting atan2(0,0) for both axes.
    const float sin_pitch = 2*(n.w*n.x+n.y*n.z);
    if (std::fabs(sin_pitch) > 0.999999f) {
        constexpr float degrees = 180.0f/3.14159265358979323846f;
        const float coupled = std::atan2(2*(n.x*n.y+n.w*n.z),
            1-2*(n.y*n.y+n.z*n.z))*degrees;
        e.roll = reference[0];
        e.pitch = std::copysign(90.0f,sin_pitch);
        e.yaw = coupled - std::copysign(1.0f,sin_pitch)*e.roll;
    }
    const std::array<float,3> candidate{
        nearest_equivalent_degrees(e.roll,reference[0]),
        nearest_equivalent_degrees(e.pitch,reference[1]),
        nearest_equivalent_degrees(e.yaw,reference[2])};
    for (float v:candidate) if (!std::isfinite(v)) return false;
    XrQuaternionf encoded{};
    if (!normalize(from_redengine_view_euler_degrees(
        candidate[0],candidate[1],candidate[2]),encoded)) return false;
    const float dot = std::fabs(n.x*encoded.x+n.y*encoded.y+n.z*encoded.z+n.w*encoded.w);
    if (!std::isfinite(dot) || dot < 0.999998f) return false;
    result = candidate;
    return true;
}

// Prepare BOTH eyes before calling either native factory. Source stays intact;
// all failures leave the caller's output untouched. Each internal camera gets
// six current-pose floats; only the PRIMARY camera gets the runtime lens.
// Pixel jitter/dimensions, history, pointers and all derived matrices stay
// untouched; the game rebuilds them while copying the private descriptors.
// Secondary visibility, temporal history, lifetime and the full renderer are
// still pending. These inputs do not prove that an eye image was rendered.
inline bool prepare_pair(const engine_camera::TemporalContract* contract,
    std::span<const std::uint8_t> source,
    const TrackingSample& tracking, Options options, PreparedPair& result) {
    using namespace openxr_eye_geometry;
    const auto version=scene_factory::selected_version(contract);
    const auto* selected_layout=engine_camera_layout::selected(contract);
    if (!scene_factory::modern(version) || !engine_camera_layout::modern(selected_layout)) return false;
    const auto policy=scene_descriptor::copy_policy(version,false);
    const auto& layout=*selected_layout;
    if (!tracking.valid || tracking.display_time <= 0 ||
        !scene_descriptor::copy_accepted(version,policy,source,source.size()) ||
        !finite(tracking.origin_position) || !std::isfinite(options.head_translation_scale) ||
        options.head_translation_scale < 0 || options.head_translation_scale > 2) return false;
    XrQuaternionf heading{};
    if (!normalize(tracking.origin_heading,heading) ||
        std::fabs(heading.x)>1e-5f || std::fabs(heading.z)>1e-5f) return false;
    EyeGeometry geometry{};
    std::array<modern_camera_projection::LensFields,2> lenses{};
    const auto primary_camera=source.subspan(layout.descriptor_cameras[0],layout.camera_bytes);
    for (std::size_t eye=0;eye<2;++eye)
        if (!modern_camera_projection::derive(contract,primary_camera,tracking.views[eye].fov,lenses[eye])) return false;
    for (const auto& view:tracking.views) {
        AsymmetricProjectionDescriptor projection{};
        if (view.type!=XR_TYPE_VIEW || view.next!=nullptr ||
            !derive_asymmetric_projection_descriptor(view.fov,1,1,projection)) return false;
        for (float angle : {view.fov.angleLeft, view.fov.angleRight,
            view.fov.angleUp, view.fov.angleDown})
            if (!std::isfinite(angle) || angle <= -1.57f || angle >= 1.57f) return false;
    }
    if (!compute(tracking.views,geometry) || geometry.baseline_m<0.04f ||
        geometry.baseline_m>0.10f || geometry.cant_degrees>45) return false;
    std::array<engine_camera_layout::PoseFields,2> bases{};
    for (std::size_t i=0;i<2;++i)
        if (!read_pose(contract,source.subspan(layout.descriptor_cameras[i],layout.camera_bytes),bases[i])) return false;
    XrQuaternionf original_anchor{}, anchor{};
    if (!normalize(orientation(bases[0]),original_anchor)) return false;
    auto level_base = bases[0];
    if (options.lock_game_pitch) level_base.rotation_degrees[1]=0;
    if (!normalize(orientation(level_base),anchor)) return false;
    const auto inverse_heading = conjugate(heading);
    const auto head_local = rotate(inverse_heading,
        subtract(geometry.cyclopean_position,tracking.origin_position));
    std::array<std::array<engine_camera_layout::PoseFields,2>,2> poses{};
    for (std::size_t eye=0;eye<2;++eye) {
        XrQuaternionf local_eye{}, final_anchor{}, rigid_delta{};
        if (!normalize(multiply(inverse_heading,geometry.eye_orientations[eye]),local_eye) ||
            !normalize(multiply(anchor,openxr_to_redengine_local_orientation(local_eye)),final_anchor) ||
            !normalize(multiply(final_anchor,conjugate(original_anchor)),rigid_delta)) return false;
        // Scale room translation only; keep the runtime's physical IPD intact.
        const auto eye_local = rotate(inverse_heading,
            subtract(tracking.views[eye].pose.position,geometry.cyclopean_position));
        const auto offset = rotate(anchor,engine_local(add(
            scaled(head_local,options.head_translation_scale),eye_local)));
        const auto eye_anchor_position = add(position(bases[0]),offset);
        if (!finite(eye_anchor_position)) return false;
        for (std::size_t i=0;i<2;++i) {
            auto& pose=poses[eye][i];
            const auto p=add(eye_anchor_position,rotate(rigid_delta,
                subtract(position(bases[i]),position(bases[0]))));
            if (!finite(p)) return false;
            pose.position={p.x,p.y,p.z};
            if (!encode_orientation(multiply(rigid_delta,orientation(bases[i])),
                bases[i].rotation_degrees,pose.rotation_degrees)) return false;
        }
    }
    PreparedPair candidate;
    candidate.contract=contract;
    candidate.display_time=tracking.display_time;
    try {
        for (std::size_t eye=0;eye<2;++eye) {
            candidate.descriptors[eye].assign(source.begin(),source.end());
            if (!scene_descriptor::copy_accepted(version,policy,candidate.descriptors[eye],source.size())) return false;
            for (std::size_t i=0;i<2;++i)
                if (!engine_camera_layout::write_pose(
                    std::span<std::uint8_t>(candidate.descriptors[eye]).subspan(
                        layout.descriptor_cameras[i],layout.camera_bytes),&layout,poses[eye][i])) return false;
            if (!modern_camera_projection::write(contract,
                std::span<std::uint8_t>(candidate.descriptors[eye]).subspan(
                    layout.descriptor_cameras[0],layout.camera_bytes),lenses[eye])) return false;
        }
    } catch (const std::bad_alloc&) { return false; }
    result=std::move(candidate);
    return true;
}
// Input identity only: this does not validate native objects, tracking generation,
// queue lifetime, rendering, or GPU completion. Both buffers publish together.
inline bool prepared_for(const PreparedPair& pair,
    const engine_camera::TemporalContract* contract, XrTime display_time) {
    const auto version=scene_factory::selected_version(contract);
    if (!scene_factory::modern(version) || pair.contract!=contract ||
        display_time<=0 || pair.display_time!=display_time) return false;
    const auto policy=scene_descriptor::copy_policy(version,false);
    for (const auto& descriptor:pair.descriptors)
        if (!scene_descriptor::copy_accepted(version,policy,descriptor,descriptor.size())) return false;
    return true;
}
} // namespace w3vr::modern_camera_pose
