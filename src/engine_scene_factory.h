#pragma once

#include <cstdint>

namespace w3vr::scene_factory {

enum class Version { unknown, legacy_404, remastered_500c };
inline constexpr std::uint32_t remastered_extent_offset = 0xBDC;

inline constexpr std::uint32_t factory_rva(Version version) {
    if (version == Version::legacy_404) return 0x016214C0;
    if (version == Version::remastered_500c) return 0x01B80550;
    return 0;
}

// Admission only, not a complete descriptor/layout or a compatibility gate.
// In the examined 5.00c producer the second parameter is NULL. Its factory
// reads settings from descriptor+0, allows them to be NULL, and rejects zero
// width/height at descriptor+0xBDC/+0xBE0. Preserve legacy admission separately.
inline bool inputs_ready(Version version, bool renderer_present,
    bool settings_parameter_present, bool descriptor_present,
    bool extent_readable, std::uint32_t width, std::uint32_t height) {
    if (!renderer_present || !descriptor_present) return false;
    if (version == Version::legacy_404) return settings_parameter_present;
    return version == Version::remastered_500c && extent_readable &&
        width != 0 && height != 0;
}

} // namespace w3vr::scene_factory
