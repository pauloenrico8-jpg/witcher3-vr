#pragma once

#include "engine_camera_temporal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace w3vr::engine_camera_layout {

// Field placement and bounded writer-route data. This does not authorize a
// game version, matrix semantics, or a complete temporal/optical-center port. The 5.00c data
// were examined in executable SHA-256 9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51.
struct Layout {
    std::size_t camera_bytes;
    std::array<std::size_t, 2> descriptor_cameras;
    std::size_t frame_descriptor;
    std::size_t jitter;
    std::size_t viewport;
    std::size_t alternate_fov;
    std::size_t descriptor_extent;
};

inline constexpr Layout legacy_404{0x510, {0x10, 0x520}, 0x10,
    0x400, 0x408, 0x468, 0xA3C};
inline constexpr Layout remastered_500c{0x5E0, {0x10, 0x5F0}, 0x10,
    0x4C0, 0x4C8, 0x538, 0xBDC};

// Share the already-selected temporal contract; never select by an INI flag
// or silently default a null/unknown contract to legacy offsets.
inline const Layout* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return &legacy_404;
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    return nullptr;
}

// Both native camera records belong to ONE scene/frame. They are not the
// left/right HMD images, which require separate rendered frames.
enum class Origin { descriptor, frame };

inline bool camera_offset(const Layout& layout, Origin origin,
    std::size_t camera_index, std::size_t& result) {
    if (camera_index >= layout.descriptor_cameras.size() ||
        (origin != Origin::descriptor && origin != Origin::frame)) return false;
    result = layout.descriptor_cameras[camera_index] +
        (origin == Origin::frame ? layout.frame_descriptor : 0);
    return true;
}

struct Fields {
    std::array<float, 14> core{};
    float alternate_fov{};
    std::array<float, 2> jitter{};
    std::array<std::uint32_t, 2> viewport{};
};

// Read raw values without interpreting matrix bytes or rejecting unknown
// camera words. All bounds must pass before the output is published. The
// caller still owns/validates the native memory; a span cannot check Windows
// page access. No writes or native calls occur here.
inline bool read_fields(std::span<const std::uint8_t> view,
    const Layout& layout, Fields& result) {
    const auto fits = [&view](std::size_t offset, std::size_t bytes) {
        return offset <= view.size() && bytes <= view.size() - offset;
    };
    if (view.size() < layout.camera_bytes ||
        !fits(0, sizeof(result.core)) ||
        !fits(layout.alternate_fov, sizeof(result.alternate_fov)) ||
        !fits(layout.jitter, sizeof(result.jitter)) ||
        !fits(layout.viewport, sizeof(result.viewport))) return false;
    Fields candidate{};
    std::memcpy(candidate.core.data(), view.data(), sizeof(candidate.core));
    std::memcpy(&candidate.alternate_fov, view.data() + layout.alternate_fov,
        sizeof(candidate.alternate_fov));
    std::memcpy(candidate.jitter.data(), view.data() + layout.jitter,
        sizeof(candidate.jitter));
    std::memcpy(candidate.viewport.data(), view.data() + layout.viewport,
        sizeof(candidate.viewport));
    result = candidate;
    return true;
}

enum class WriterPurpose { unknown, normal_temporal, supersample_apply, supersample_restore,
    final_2d_override };

struct WriterCandidate {
    std::uintptr_t return_rva;
    WriterPurpose purpose;
};

inline constexpr std::uint32_t remastered_writer_rva = 0x022B51E0;
inline constexpr std::array<WriterCandidate, 4> remastered_writer_candidates{{
    {0x01C14BD1, WriterPurpose::normal_temporal},
    {0x01D59287, WriterPurpose::supersample_apply},
    {0x01D59880, WriterPurpose::supersample_restore},
    {0x01D59D27, WriterPurpose::final_2d_override}}};

inline WriterPurpose remastered_writer_purpose(std::uintptr_t return_rva) {
    for (const auto& route : remastered_writer_candidates) {
        if (route.return_rva == return_rva) return route.purpose;
    }
    return WriterPurpose::unknown;
}

inline constexpr std::array<std::uintptr_t, 8> legacy_writer_returns{
    0x01553A78, 0x01553E1B, 0x01D861E3, 0x01D86DBC,
    0x01D86EA5, 0x01D87606, 0x01D87925, 0x01D87EC0};

// Entry/route selection is reached only after the main version preflight.
// Adding the 5.00c data does not change that preflight's rejection of 5.00c.
inline std::uintptr_t temporal_writer_rva(
    const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return 0x015E5A90;
    if (contract == &engine_camera::remastered_500c) return remastered_writer_rva;
    return 0;
}

inline bool normal_temporal_route(const engine_camera::TemporalContract* contract,
    std::uintptr_t return_rva) {
    if (contract == &engine_camera::legacy_404) {
        for (const auto rva : legacy_writer_returns) if (rva == return_rva) return true;
    }
    if (contract == &engine_camera::remastered_500c)
        return remastered_writer_purpose(return_rva) == WriterPurpose::normal_temporal;
    return false;
}

inline bool legacy_centered_writer_hint(
    const engine_camera::TemporalContract* contract, std::uintptr_t return_rva) {
    return contract == &engine_camera::legacy_404 && return_rva == 0x01D87EC0;
}

// Current jitter, not previous jitter or the history-record block. This
// setter's RCX is a descriptor; it is not the containing frame pointer.
inline bool descriptor_jitter_offsets(const engine_camera::TemporalContract* contract,
    std::array<std::size_t, 2>& result) {
    const auto* layout = selected(contract);
    if (layout == nullptr) return false;
    result = {layout->descriptor_cameras[0] + layout->jitter,
              layout->descriptor_cameras[1] + layout->jitter};
    return true;
}

// These new calls are NOT one-to-one replacements for the eight legacy routes. The
// supersampling loop restores values it captured; adding the optical center
// again on restore would accumulate it. Final2D's inputs need a separate
// ownership/source contract. Do not install either an old RVA or this candidate
// as a complete Remastered port merely because its ABI matches. The normal
// call was reached through the validated mode table at 01C14B72. The builder
// separately stores previous jitter at descriptor+4E0/+4E4 AFTER the setter;
// that write is not proof that the previous projection has been ported.
inline bool legacy_temporal_writer_allowed(
    const engine_camera::TemporalContract* contract) {
    return contract == &engine_camera::legacy_404;
}

} // namespace w3vr::engine_camera_layout
