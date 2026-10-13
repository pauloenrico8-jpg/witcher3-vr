#pragma once
#include "engine_camera_temporal.h"

namespace w3vr::camera_history_projection {
inline constexpr std::size_t projection_offset=0x70;
// Independently examined fresh perspective record for the current build only.
// A contract pointer identifies a static profile, never a live Game object.
// This helper transforms OWN transient records; it admits no Native call.
inline bool center_fresh_record(const engine_camera::TemporalContract* contract,
    const std::array<float,2>& center,const engine_camera::TemporalRecord& fresh,
    engine_camera::TemporalRecord& out) noexcept {
    if(contract!=&engine_camera::remastered_1048522 || fresh[0]!=1 ||
        !std::isfinite(center[0]) || !std::isfinite(center[1]))return false;
    float time{};std::memcpy(&time,fresh.data()+4,4);
    std::array<float,16> projection{};
    std::memcpy(projection.data(),fresh.data()+projection_offset,sizeof(projection));
    if(!std::isfinite(time))return false;
    for(float x:projection)if(!std::isfinite(x))return false;
    if(projection[0]<=0 || projection[5]<=0 || projection[11]!=1)return false;
    // Refuse orthographic, already shifted, arbitrary or malformed matrices.
    // For row-vector P and identity shift S, P*S adds center.x/y times
    // clip W to clip X/Y. Fresh perspective W=Z: only P[2][0/1] change.
    // Depth, W, view, pose, FOV, time and destination padding stay intact.
    for(std::size_t index:{1u,2u,3u,4u,6u,7u,8u,9u,12u,13u,15u})
        if(projection[index]!=0)return false;
    engine_camera::TemporalRecord staged=fresh;
    std::memcpy(staged.data()+projection_offset+8*sizeof(float),&center[0],4);
    std::memcpy(staged.data()+projection_offset+9*sizeof(float),&center[1],4);
    out=staged;return true;
}
} // namespace w3vr::camera_history_projection
