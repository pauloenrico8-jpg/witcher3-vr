#pragma once

#include "engine_camera_layout.h"
#include "openxr_eye_geometry.h"
#include <array>
#include <cmath>
#include <cstring>
#include <span>

namespace w3vr::modern_camera_projection {
// Current primary-camera lens inputs only. The examined modern rebuild applies
// the stable normalized shift AFTER its pixel jitter. Keep these independent:
// a native temporal-writer call must not remove or duplicate the HMD center.
// These fields are not a matrix receipt, render permission or GPU completion.
struct LensFields {
    float vertical_fov_degrees{};
    float aspect{};
    float projection_scale{1};
    std::array<float,2> center_ndc{};
};
inline constexpr std::size_t fov_offset=0x1C;
inline constexpr std::size_t aspect_offset=0x28;
inline constexpr std::size_t scale_offset=0x2C;
inline constexpr std::size_t stable_center_offset=0x520;
inline bool valid(const LensFields& lens) {
    return std::isfinite(lens.vertical_fov_degrees) &&
        lens.vertical_fov_degrees>0 && lens.vertical_fov_degrees<180 &&
        std::isfinite(lens.aspect) && lens.aspect>0 &&
        lens.projection_scale==1 && std::isfinite(lens.center_ndc[0]) &&
        std::isfinite(lens.center_ndc[1]);
}
inline bool derive(std::span<const std::uint8_t> camera,
    const XrFovf& fov, LensFields& result) {
    if (camera.size()<engine_camera_layout::remastered_500c.camera_bytes) return false;
    // Preserve the game's depth range and require a valid perspective source.
    // XYZ/W/rotation, temporal history and embedded references are not written.
    engine_camera::TemporalInputs source{};
    if (!engine_camera::read_temporal_inputs(camera,0,source) ||
        source.fov>=180 || source.projection_scale<=0) return false;
    for (float angle:{fov.angleLeft,fov.angleRight,fov.angleUp,fov.angleDown})
        if (!std::isfinite(angle) || angle<=-1.57f || angle>=1.57f) return false;
    openxr_eye_geometry::AsymmetricProjectionDescriptor descriptor{};
    // Only angular spans and normalized centers are used. No fake viewport is
    // written: all native integer dimensions and pixel jitter remain intact.
    if (!openxr_eye_geometry::derive_asymmetric_projection_descriptor(fov,1,1,descriptor)) return false;
    const LensFields candidate{descriptor.vertical_fov_degrees,descriptor.aspect,1,
        {descriptor.center_ndc_x,descriptor.center_ndc_y}};
    if (!valid(candidate)) return false;
    result=candidate;
    return true;
}
inline bool write(std::span<std::uint8_t> camera, const LensFields& lens) {
    if (camera.size()<engine_camera_layout::remastered_500c.camera_bytes || !valid(lens)) return false;
    // Replace the game's current lens with the runtime lens, including its
    // absolute center. Never add to a possibly nonzero source center. The game
    // builds current/derived matrices while copying the private descriptor.
    // This bounded private-buffer write is not an atomic native-memory update.
    std::memcpy(camera.data()+fov_offset,&lens.vertical_fov_degrees,sizeof(float));
    std::memcpy(camera.data()+aspect_offset,&lens.aspect,sizeof(float));
    std::memcpy(camera.data()+scale_offset,&lens.projection_scale,sizeof(float));
    std::memcpy(camera.data()+stable_center_offset,lens.center_ndc.data(),sizeof(lens.center_ndc));
    return true;
}
} // namespace w3vr::modern_camera_projection
