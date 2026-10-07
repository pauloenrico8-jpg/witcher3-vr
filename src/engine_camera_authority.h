#pragma once
#include "engine_camera_temporal.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace w3vr::engine_camera_authority {
// Versioned script callback entries and READ-ONLY authority fields. The 5.00c
// candidates were checked against the installed executable; this subsystem
// does not authorize its renderer or enable the global version preflight.
enum class Callback : std::size_t { head, bone_matrix, actor_gameplay,
    actor_cutscene, game_cutscene, manual_control, top_camera, loading_video,
    direction, count };
struct Entry { std::uintptr_t rva; std::span<const std::uint8_t> signature; };
inline constexpr std::array<std::uint8_t, 26> head_signature{0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF, 0x42, 0x30, 0x48, 0x81, 0xC1, 0x00, 0x02, 0x00, 0x00, 0x49, 0x8B, 0xD8, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x38};
inline constexpr std::array<std::uint8_t, 27> bone_matrix_signature{0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00};
inline constexpr std::array<std::uint8_t, 20> actor_gameplay_signature{0x48, 0xFF, 0x42, 0x30, 0x4D, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x81, 0xF3, 0x02, 0x00, 0x00, 0x41, 0x88, 0x00, 0xC3};
inline constexpr std::array<std::uint8_t, 20> actor_cutscene_signature{0x48, 0xFF, 0x42, 0x30, 0x4D, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x81, 0xF2, 0x02, 0x00, 0x00, 0x41, 0x88, 0x00, 0xC3};
inline constexpr std::array<std::uint8_t, 20> game_cutscene_signature{0x48, 0xFF, 0x42, 0x30, 0x4D, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x81, 0x1F, 0x01, 0x00, 0x00, 0x41, 0x88, 0x00, 0xC3};
inline constexpr std::array<std::uint8_t, 24> manual_control_signature{0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x42, 0x30, 0x4C, 0x8D, 0x0D, 0x4B, 0x44, 0xC6, 0x03, 0x48, 0x8B, 0xD9};
inline constexpr std::array<std::uint8_t, 24> top_camera_signature{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF, 0x42, 0x30, 0x45, 0x33, 0xC9, 0x8B, 0x41, 0x60, 0x41, 0x8B, 0xD9, 0xFF};
inline constexpr std::array<std::uint8_t, 24> loading_video_signature{0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF, 0x42, 0x30, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0x99, 0x98, 0x01, 0x00, 0x00};
inline constexpr std::array<std::uint8_t, 24> direction_signature{0x48, 0xFF, 0x42, 0x30, 0xC5, 0xFA, 0x10, 0x81, 0x90, 0x07, 0x00, 0x00, 0xC5, 0xFA, 0x10, 0x89, 0x94, 0x07, 0x00, 0x00, 0xC5, 0xFA, 0x10, 0x91};
struct Profile {
    std::array<Entry, static_cast<std::size_t>(Callback::count)> entries;
    std::size_t actor_gameplay, actor_cutscene, game_cutscene, manual_control;
    std::size_t director_entries, director_count, entry_stride, native_camera;
    std::uintptr_t custom_camera_vtable;
};
inline constexpr Profile legacy_404{{{
    Entry{0x01C4B550, head_signature},
    Entry{0x0152E600, bone_matrix_signature},
    Entry{0x01C4CD70, {}},
    Entry{0x01C4CD90, {}},
    Entry{0x01549800, {}},
    Entry{0x01F580E0, {}},
    Entry{0x0161B9C0, {}},
    Entry{0x0154EED0, {}},
    Entry{0x0161BBC0, {}},
}}, 0x2F3, 0x2F2, 0x11F, 0x259, 0x58, 0x60, 0x28, 0, 0x02D645B0};
inline constexpr Profile remastered_500c{{{
    Entry{0x02102690, head_signature},
    Entry{0x02251A40, bone_matrix_signature},
    Entry{0x02103880, actor_gameplay_signature},
    Entry{0x021038A0, actor_cutscene_signature},
    Entry{0x0226CDB0, game_cutscene_signature},
    Entry{0x01E07620, manual_control_signature},
    Entry{0x0237A0A0, top_camera_signature},
    Entry{0x02271A80, loading_video_signature},
    Entry{0x0237A1C0, direction_signature},
}}, 0x2F3, 0x2F2, 0x11F, 0x269, 0x58, 0x60, 0x28, 0, 0x03805378};
inline const Profile* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return &legacy_404;
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    return nullptr;
}
inline const Entry* entry(const Profile* profile, Callback callback) {
    const auto index = static_cast<std::size_t>(callback);
    return profile && index < profile->entries.size() ? &profile->entries[index] : nullptr;
}
inline bool is_custom_camera(const engine_camera::TemporalContract* contract,
    std::uintptr_t vtable_rva) {
    const auto* profile = selected(contract);
    return profile && vtable_rva != 0 && vtable_rva == profile->custom_camera_vtable;
}
// Bounds checking does not establish native page access, object lifetime, or
// thread ordering. Host reads remain guarded and cannot write these fields.
inline bool read_flag(std::span<const std::uint8_t> object, std::size_t offset, int& output) {
    if (offset >= object.size()) return false;
    output = object[offset] != 0 ? 1 : 0;
    return true;
}
inline bool signature_matches(const Entry& callback, std::span<const std::uint8_t> bytes) {
    if (callback.rva == 0 || callback.signature.empty() || bytes.size() < callback.signature.size()) return false;
    for (std::size_t i = 0; i < callback.signature.size(); ++i)
        if (bytes[i] != callback.signature[i]) return false;
    return true;
}
} // namespace w3vr::engine_camera_authority
