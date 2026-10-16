#pragma once
#include "engine_render_core.h"
#include <cstdint>
#include <span>

namespace w3vr::modern_eye_capture {
// Synchronous native core/task metadata. Not retained engine/COM ownership or
// GPU completion. Frame is a copied registry key, never dereferenced here.
struct Identity {
    const render_core::Profile* profile{};
    std::uintptr_t renderer{};
    render_core::FrameLabel frame{};
};
inline bool admits(const Identity& identity,
    const engine_camera::TemporalContract* contract,
    const render_core::Profile* installed, bool ready, std::uint32_t generation) {
    const auto* selected = render_core::modern_selected(contract);
    return xr_frame::valid(identity.frame.xr) && selected && identity.profile == selected &&
        render_core::ready_for(contract, installed, ready) &&
        render_core::accepts_frame_label(identity.renderer, identity.frame.frame,
            &identity.frame, generation);
}
// Numeric D3D12 states, independent of Windows headers in these CPU tests.
// A backbuffer has one mip and one array slice. Require a completed, whole
// transition from an actual write state to COMMON/PRESENT, both numerically0.
// Exact current swapchain resource and recording endpoint are checked by host.
struct Transition {
    std::uint32_t flags{}, before{}, after{}, subresource{};
};
inline bool finished_backbuffer(const Transition& transition) {
    return transition.flags == 0 && transition.after == 0 &&
        (transition.before == 0x4 || transition.before == 0x400) &&
        (transition.subresource == 0 || transition.subresource == UINT32_MAX);
}
// One tagged eye scope may write the backbuffer more than once before PRESENT
// (e.g. COPY_DEST(0x400)->PRESENT for a copied scene, then RENDER_TARGET(0x4)
// ->PRESENT after an overlay pass). Each finished transition records its own
// copy; actual Execute (GPU) order publishes the latest one. The finished eye
// is therefore the LAST copy of that eye/pair: never promote an earlier copy
// while a later copy of the same pair is recorded but not yet submitted.
struct RecordedCopy {
    std::uint64_t serial{}, pair{};
    bool pending{};
};
inline bool later_copy_pending(std::uint64_t published_serial, std::uint64_t pair,
    std::span<const RecordedCopy> copies) {
    if (pair == 0 || pair == UINT64_MAX) return false;
    for (const auto& copy : copies)
        if (copy.pending && copy.pair == pair && copy.serial > published_serial) return true;
    return false;
}
struct Request {
    std::uintptr_t command{}, source{};
    std::uint32_t generation{};
    std::uint64_t pair{};
    int eye{-1};
    xr_frame::Identity xr{};
};
// Creates an immutable COPY request, never a ready image. Caller must record
// the real copy now, retain its own textures, publish on actual Execute and
// wait for its GPU fence. Do not cache one eye for an entire shared list.
inline bool prepare(const Identity& identity,
    const engine_camera::TemporalContract* contract,
    const render_core::Profile* installed, bool ready, std::uint32_t generation,
    std::uintptr_t command, bool recording_endpoint, std::uintptr_t source,
    bool current_backbuffer, bool single_subresource, const Transition& transition,
    Request& result) {
    if (!admits(identity,contract,installed,ready,generation) || !command ||
        !recording_endpoint || !source || !current_backbuffer ||
        !single_subresource || !finished_backbuffer(transition)) return false;
    result = {command,source,generation,identity.frame.pair,identity.frame.eye,identity.frame.xr};
    return true;
}
} // namespace w3vr::modern_eye_capture
