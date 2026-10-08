#pragma once

#include "engine_camera_layout.h"
#include "engine_scene_descriptor.h"
#include "openxr_eye_geometry.h"
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
inline bool read_pose(std::span<const std::uint8_t> camera,
    engine_camera_layout::PoseFields& pose) {
    if (camera.size() < engine_camera_layout::remastered_500c.camera_bytes) return false;
    std::memcpy(pose.position.data(), camera.data(), sizeof(pose.position));
    std::memcpy(pose.rotation_degrees.data(), camera.data()+0x10, sizeof(pose.rotation_degrees));
    for (float v : pose.position) if (!std::isfinite(v)) return false;
    for (float v : pose.rotation_degrees) if (!std::isfinite(v)) return false;
    float fov{};
    std::memcpy(&fov, camera.data()+0x1C, sizeof(fov));
    return std::isfinite(fov) && fov > 0 && fov < 180; // Perspective only.
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
// all failures leave the caller's output untouched. Only six current-pose
// floats per internal camera change. Projection, history, pointers and all
// derived matrices remain native inputs; the game rebuilds them from the new
// pose while copying these private descriptors. This does not port projection,
// history ownership, task lifetime, culling consumers or the full renderer.
inline bool prepare_pair(std::span<const std::uint8_t> source,
    const TrackingSample& tracking, Options options, PreparedPair& result) {
    using namespace openxr_eye_geometry;
    constexpr auto version = scene_factory::Version::remastered_500c;
    constexpr auto policy = scene_descriptor::copy_policy(version,false);
    const auto& layout = engine_camera_layout::remastered_500c;
    if (!tracking.valid || tracking.display_time <= 0 ||
        !scene_descriptor::copy_accepted(version,policy,source,source.size()) ||
        !finite(tracking.origin_position) || !std::isfinite(options.head_translation_scale) ||
        options.head_translation_scale < 0 || options.head_translation_scale > 2) return false;
    XrQuaternionf heading{};
    if (!normalize(tracking.origin_heading,heading) ||
        std::fabs(heading.x)>1e-5f || std::fabs(heading.z)>1e-5f) return false;
    EyeGeometry geometry{};
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
        if (!read_pose(source.subspan(layout.descriptor_cameras[i],layout.camera_bytes),bases[i])) return false;
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
    try {
        for (std::size_t eye=0;eye<2;++eye) {
            candidate.descriptors[eye].assign(source.begin(),source.end());
            if (!scene_descriptor::copy_accepted(version,policy,candidate.descriptors[eye],source.size())) return false;
            for (std::size_t i=0;i<2;++i)
                if (!engine_camera_layout::write_pose(
                    std::span<std::uint8_t>(candidate.descriptors[eye]).subspan(
                        layout.descriptor_cameras[i],layout.camera_bytes),&layout,poses[eye][i])) return false;
        }
    } catch (const std::bad_alloc&) { return false; }
    result=std::move(candidate);
    return true;
}
} // namespace w3vr::modern_camera_pose
