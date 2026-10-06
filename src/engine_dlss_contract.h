#pragma once

#include "engine_camera_temporal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace w3vr::engine_dlss {

// These fields are independently observed in the native 5.00c methods.
// The render counter and the Streamline token index are different fields;
// builder+120 uses C44, whereas evaluation+124 and preparation+128 use C40.
// This read-only contract does NOT authorize rearming any of those guards.
struct Layout {
    std::size_t descriptor_render_counter;
    std::size_t descriptor_token_index;
    std::size_t state_evaluated_counter;
    std::size_t state_prepared_counter;
};
inline constexpr Layout remastered_500c{0xC40, 0xC44, 0x124, 0x128};

inline const Layout* selected(const engine_camera::TemporalContract* contract) {
    return contract == &engine_camera::remastered_500c ? &remastered_500c : nullptr;
}

struct Counters {
    std::uint32_t render_counter{};
    std::uint32_t token_index{};
    std::uint32_t evaluated_counter{};
    std::uint32_t prepared_counter{};
};

inline bool read_counters(std::span<const std::uint8_t> state,
    std::span<const std::uint8_t> descriptor, const Layout* layout, Counters& result) {
    if (layout != &remastered_500c || state.size() < 0x12C || descriptor.size() < 0xC48)
        return false;
    Counters candidate{};
    std::memcpy(&candidate.render_counter, descriptor.data() + layout->descriptor_render_counter, 4);
    std::memcpy(&candidate.token_index, descriptor.data() + layout->descriptor_token_index, 4);
    std::memcpy(&candidate.evaluated_counter, state.data() + layout->state_evaluated_counter, 4);
    std::memcpy(&candidate.prepared_counter, state.data() + layout->state_prepared_counter, 4);
    result = candidate;
    return true;
}

// Native constructors and the public Streamline definition both identify
// this plain (non-polymorphic) ViewportHandle header and +20 value. FrameToken
// is polymorphic and opaque: this parser must NEVER be used to read a token.
inline constexpr std::array<std::uint32_t, 4> viewport_type{
    0x171B6435, 0x4FC89B3C, 0xE5FB9499, 0xA4AA6925};
inline constexpr std::size_t viewport_bytes = 0x28;
struct Viewport {
    std::uint32_t id{};
};

inline bool read_viewport(std::span<const std::uint8_t> bytes, Viewport& result) {
    if (bytes.size() < viewport_bytes) return false;
    std::uint64_t next{}, version{};
    std::array<std::uint32_t, 4> type{};
    std::memcpy(&next, bytes.data(), 8);
    std::memcpy(type.data(), bytes.data() + 8, 16);
    std::memcpy(&version, bytes.data() + 0x18, 8);
    // Unexamined chains and newer revisions still pass through the SDK, but
    // they cannot serve as evidence for our single-viewport native path.
    if (next != 0 || type != viewport_type || version != 1) return false;
    Viewport candidate{};
    std::memcpy(&candidate.id, bytes.data() + 0x20, 4);
    if (candidate.id == UINT32_MAX) return false;
    result = candidate;
    return true;
}

inline constexpr std::uint32_t dlss = 0;
inline constexpr std::uint32_t ray_reconstruction = 1001;
inline constexpr std::uintptr_t dlss_evaluate_return = 0x01ED3230;
inline constexpr std::uintptr_t ray_reconstruction_evaluate_return = 0x01ED3F90;
inline constexpr std::array<std::uintptr_t, 2> tag_returns{0x01ED2ADA, 0x01ED2BA0};

inline bool known_evaluate_return(std::uint32_t feature, std::uintptr_t site) {
    return (feature == dlss && site == dlss_evaluate_return) ||
        (feature == ray_reconstruction && site == ray_reconstruction_evaluate_return);
}
inline bool known_tag_return(std::uintptr_t site) {
    return site == tag_returns[0] || site == tag_returns[1];
}

// References in the SDK are pointers at the Windows x64 ABI. These modern
// signatures deliberately do not share the legacy Streamline 1.5 trampolines.
// The command buffer is the FIFTH argument, not the first legacy argument.
using SetTagForFrameFn = int (*)(const void* token, const void* viewport,
    const void* tags, std::uint32_t count, void* command_buffer);
using EvaluateFn = int (*)(std::uint32_t feature, const void* token,
    const void** inputs, std::uint32_t count, void* command_buffer);

struct TagCall {
    const void* token{};
    const void* viewport{};
    const void* tags{};
    std::uint32_t count{};
    void* command_buffer{};
};
struct EvaluateCall {
    std::uint32_t feature{};
    const void* token{};
    const void** inputs{};
    std::uint32_t count{};
    void* command_buffer{};
};

inline int forward(SetTagForFrameFn original, const TagCall& call) {
    return original(call.token, call.viewport, call.tags, call.count, call.command_buffer);
}
inline int forward(EvaluateFn original, const EvaluateCall& call) {
    return original(call.feature, call.token, call.inputs, call.count, call.command_buffer);
}

// A validated SDK call is still not a receipt of an eye's render, temporal
// isolation, execution on the GPU, or completion of that execution.
inline constexpr bool stereo_reentry_verified = false;

} // namespace w3vr::engine_dlss
