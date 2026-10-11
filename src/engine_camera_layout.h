#pragma once

#include "engine_camera_temporal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace w3vr::engine_camera_layout {

// Field placement and bounded writer-route data. This does not authorize a
// game version, matrix semantics, or a complete temporal/optical-center port.
// Each modern profile was checked independently against its executable.
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
inline constexpr Layout remastered_1048522{0x5E0, {0x10, 0x5F0}, 0x10,
    0x4C0, 0x4C8, 0x538, 0xBDC};
inline bool modern(const Layout* layout) {
    return layout == &remastered_500c || layout == &remastered_1048522;
}
inline bool known(const Layout* layout) {
    return layout == &legacy_404 || modern(layout);
}

// Share the already-selected temporal contract; never select by an INI flag
// or silently default a null/unknown contract to legacy offsets.
inline const Layout* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return &legacy_404;
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    if (contract == &engine_camera::remastered_1048522) return &remastered_1048522;
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

// Current optical-center/jitter floats followed by INTEGER input dimensions.
// A known layout checks byte placement only, not ownership or native lifetime.
struct ProjectionFields {
    std::array<float, 2> center_px{};
    std::array<std::uint32_t, 2> viewport{};
};
static_assert(sizeof(ProjectionFields) == 16);
static_assert(offsetof(ProjectionFields, viewport) == 8);
inline bool projection_range(std::size_t bytes, const Layout* layout) {
    return known(layout) &&
        bytes >= layout->camera_bytes && layout->jitter <= bytes &&
        sizeof(ProjectionFields) <= bytes - layout->jitter &&
        layout->viewport == layout->jitter + sizeof(ProjectionFields::center_px);
}
inline bool read_projection(std::span<const std::uint8_t> view,
    const Layout* layout, ProjectionFields& result) {
    if (!projection_range(view.size(), layout)) return false;
    ProjectionFields candidate{};
    std::memcpy(&candidate, view.data() + layout->jitter, sizeof(candidate));
    result = candidate;
    return true;
}
inline bool write_projection(std::span<std::uint8_t> view,
    const Layout* layout, const ProjectionFields& fields) {
    if (!projection_range(view.size(), layout) ||
        !std::isfinite(fields.center_px[0]) || !std::isfinite(fields.center_px[1]) ||
        fields.viewport[0] == 0 || fields.viewport[1] == 0) return false;
    // All validation happens before a single bounded copy. This is not an
    // atomic native store, a page-access guarantee, or a render capability.
    std::memcpy(view.data() + layout->jitter, &fields, sizeof(fields));
    return true;
}

struct PoseFields {
    std::array<float, 3> position{};
    std::array<float, 3> rotation_degrees{};
};
// The six source pose floats have the same placement in the examined profiles.
// Keep position W, FOV, matrices, projection, history and adjacent objects intact.
inline bool write_pose(std::span<std::uint8_t> view,
    const Layout* layout, const PoseFields& pose) {
    if (!known(layout) ||
        view.size() < layout->camera_bytes) return false;
    for (float value : pose.position) if (!std::isfinite(value)) return false;
    for (float value : pose.rotation_degrees) if (!std::isfinite(value)) return false;
    std::memcpy(view.data(), pose.position.data(), sizeof(pose.position));
    std::memcpy(view.data() + 0x10, pose.rotation_degrees.data(), sizeof(pose.rotation_degrees));
    return true;
}

enum class WriterPurpose { unknown, normal_temporal, supersample_apply, supersample_restore,
    final_2d_override };

struct WriterCandidate {
    std::uintptr_t return_rva;
    WriterPurpose purpose;
};

struct WriterProfile {
    std::uintptr_t entry;
    std::array<WriterCandidate, 4> routes;
    std::array<std::uint8_t, 15> prefix;
};
inline constexpr WriterProfile writer_500c{0x022B51E0, {{
    {0x01C14BD1, WriterPurpose::normal_temporal},
    {0x01D59287, WriterPurpose::supersample_apply},
    {0x01D59880, WriterPurpose::supersample_restore},
    {0x01D59D27, WriterPurpose::final_2d_override}}},
    {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40}};
inline constexpr WriterProfile writer_1048522{0x022BD750, {{
    {0x01C1B7B1, WriterPurpose::normal_temporal},
    {0x01D602B7, WriterPurpose::supersample_apply},
    {0x01D608B0, WriterPurpose::supersample_restore},
    {0x01D60D57, WriterPurpose::final_2d_override}}},
    {0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x40}};
inline const WriterProfile* selected_writer(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::remastered_500c) return &writer_500c;
    if (contract == &engine_camera::remastered_1048522) return &writer_1048522;
    return nullptr;
}
inline bool writer_prefix_matches(const WriterProfile* profile,
    std::span<const std::uint8_t> bytes) {
    if ((profile != &writer_500c && profile != &writer_1048522) ||
        bytes.size() < profile->prefix.size()) return false;
    for (std::size_t i=0; i<profile->prefix.size(); ++i)
        if (bytes[i] != profile->prefix[i]) return false;
    return true;
}
inline WriterPurpose remastered_writer_purpose(
    const engine_camera::TemporalContract* contract, std::uintptr_t return_rva) {
    const auto* profile = selected_writer(contract);
    if (profile != nullptr) for (const auto& route : profile->routes)
        if (route.return_rva == return_rva) return route.purpose;
    return WriterPurpose::unknown;
}

inline constexpr std::array<std::uintptr_t, 8> legacy_writer_returns{
    0x01553A78, 0x01553E1B, 0x01D861E3, 0x01D86DBC,
    0x01D86EA5, 0x01D87606, 0x01D87925, 0x01D87EC0};

// Entry/route selection is reached only after the main version preflight.
// Selecting a subsystem profile does not change global render admission.
inline std::uintptr_t temporal_writer_rva(
    const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return 0x015E5A90;
    const auto* profile = selected_writer(contract);
    return profile != nullptr ? profile->entry : 0;
}

inline bool normal_temporal_route(const engine_camera::TemporalContract* contract,
    std::uintptr_t return_rva) {
    if (contract == &engine_camera::legacy_404) {
        for (const auto rva : legacy_writer_returns) if (rva == return_rva) return true;
    }
    return remastered_writer_purpose(contract, return_rva) == WriterPurpose::normal_temporal;
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

// Modern callers have distinct source contracts. Supersampling restores values
// it captured; adding a center on restore would accumulate it. Final2D inputs
// remain separate. The normal call is reached through the checked mode table,
// and previous jitter is written separately after it. These bounded routes do
// not authorize legacy temporal corrections, history isolation or reentry.
inline bool legacy_temporal_writer_allowed(
    const engine_camera::TemporalContract* contract) {
    return contract == &engine_camera::legacy_404;
}

} // namespace w3vr::engine_camera_layout
