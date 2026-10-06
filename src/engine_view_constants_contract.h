#pragma once

#include "engine_camera_temporal.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace w3vr::engine_view_constants {

// A subsystem contract, not permission to start the game hooks. The 5.00c
// builder's native RCX is the state obtained by its caller from renderer+C0;
// RDX is the descriptor, not the enclosing frame. No legacy global/vtable
// getter, evaluation guard, or input-preparation guard is asserted for 5.00c.
struct Layout {
    std::uint32_t builder_rva;
    std::size_t descriptor_frame_id;
    std::size_t state_builder_frame;
};
inline constexpr Layout legacy_404{0x01CFE280, 0xAA4, 0x6C};
inline constexpr Layout remastered_500c{0x01B799C0, 0xC44, 0x120};

inline const Layout* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return &legacy_404;
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    return nullptr;
}

struct BuilderSnapshot {
    std::uint32_t frame_id{};
    std::uint32_t guard_frame{};
};

inline bool read_builder(std::span<const std::uint8_t> state,
    std::span<const std::uint8_t> descriptor, const Layout* layout,
    BuilderSnapshot& result) {
    if ((layout != &legacy_404 && layout != &remastered_500c) ||
        layout->state_builder_frame > state.size() ||
        sizeof(std::uint32_t) > state.size() - layout->state_builder_frame ||
        layout->descriptor_frame_id > descriptor.size() ||
        sizeof(std::uint32_t) > descriptor.size() - layout->descriptor_frame_id) {
        return false;
    }
    BuilderSnapshot candidate{};
    std::memcpy(&candidate.frame_id, descriptor.data() + layout->descriptor_frame_id,
        sizeof(candidate.frame_id));
    std::memcpy(&candidate.guard_frame, state.data() + layout->state_builder_frame,
        sizeof(candidate.guard_frame));
    result = candidate;
    return true;
}

// Until modern resource tags/evaluation/viewport routing are ported together,
// rearming the modern builder would overwrite the same native viewport. This
// explicit capability gate must stay closed even if its address is known.
inline bool legacy_reentry_supported(const engine_camera::TemporalContract* contract) {
    return contract == &engine_camera::legacy_404;
}

inline constexpr std::uintptr_t remastered_builder_return = 0x01C14C26;
inline constexpr std::uintptr_t remastered_constants_return = 0x01B7A5EA;

// Constants v2 in the examined 5.00c executable has the public Streamline
// BaseStructure header: next pointer, type GUID, 64-bit version. Native
// constructor 0x00320940 and the builder independently confirm these offsets.
// The FrameToken remains opaque; never truncate its pointer to a frame number.
inline constexpr std::size_t remastered_constants_bytes = 0x1C8;
inline constexpr std::array<std::uint32_t, 4> constants_type{
    0xDCD35AD7, 0x4BAD4E4A, 0xC4E00CA9, 0xFE3AB29E};
struct ConstantsSnapshot {
    std::array<float, 2> jitter{};
    std::array<float, 2> motion_vector_scale{};
    bool reset{};
};

inline bool read_remastered_constants(std::span<const std::uint8_t> bytes,
    ConstantsSnapshot& result) {
    if (bytes.size() < remastered_constants_bytes) return false;
    std::array<std::uint32_t, 4> type{};
    std::uint64_t version{};
    std::memcpy(type.data(), bytes.data() + 8, sizeof(type));
    std::memcpy(&version, bytes.data() + 0x18, sizeof(version));
    if (type != constants_type || version != 2 || bytes[0x1BF] > 1) return false;
    ConstantsSnapshot candidate{};
    std::memcpy(candidate.jitter.data(), bytes.data() + 0x160,
        sizeof(candidate.jitter));
    std::memcpy(candidate.motion_vector_scale.data(), bytes.data() + 0x168,
        sizeof(candidate.motion_vector_scale));
    for (float value : candidate.jitter) if (!std::isfinite(value)) return false;
    for (float value : candidate.motion_vector_scale) if (!std::isfinite(value)) return false;
    candidate.reset = bytes[0x1BF] == 1;
    result = candidate;
    return true;
}

struct Observation {
    const void* state{};
    const void* descriptor{};
    std::uint64_t pair_id{};
    std::uint32_t generation{};
    std::uint32_t frame_id{};
    int eye{-1};
};

// Both actual native return sites and the unchanged render identity must be
// present. A coincident frame id, stale pair, or another caller is insufficient.
inline bool receipt_allowed(const engine_camera::TemporalContract* contract,
    const Observation& observation, std::uintptr_t constants_return,
    std::uint64_t pair_id, std::uint32_t generation, int eye,
    int native_result, bool constants_valid) {
    return contract == &engine_camera::remastered_500c &&
        constants_return == remastered_constants_return &&
        observation.state != nullptr && observation.descriptor != nullptr &&
        pair_id != 0 && pair_id != UINT64_MAX &&
        observation.pair_id == pair_id && observation.generation == generation &&
        (eye == 0 || eye == 1) && observation.eye == eye &&
        native_result == 0 && constants_valid;
}

} // namespace w3vr::engine_view_constants
