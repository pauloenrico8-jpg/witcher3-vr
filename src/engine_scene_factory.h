#pragma once

#include "engine_camera_temporal.h"
#include <cstdint>

namespace w3vr::scene_factory {

enum class Version { unknown, legacy_404, remastered_500c, remastered_1048522 };
inline constexpr bool modern(Version version) {
    return version == Version::remastered_500c || version == Version::remastered_1048522;
}
inline Version selected_version(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return Version::legacy_404;
    if (contract == &engine_camera::remastered_500c) return Version::remastered_500c;
    if (contract == &engine_camera::remastered_1048522) return Version::remastered_1048522;
    return Version::unknown;
}
inline constexpr std::uint32_t remastered_extent_offset = 0xBDC;

inline constexpr std::uint32_t factory_rva(Version version) {
    if (version == Version::legacy_404) return 0x016214C0;
    if (version == Version::remastered_500c) return 0x01B80550;
    if (version == Version::remastered_1048522) return 0x01B86E10;
    return 0;
}

// Admission only, not a complete descriptor/layout or a compatibility gate.
// In the independently examined modern producers the second parameter is NULL.
// Each corresponding factory
// reads settings from descriptor+0, allows them to be NULL, and rejects zero
// width/height at descriptor+0xBDC/+0xBE0. Preserve legacy admission separately.
inline bool inputs_ready(Version version, bool renderer_present,
    bool settings_parameter_present, bool descriptor_present,
    bool extent_readable, std::uint32_t width, std::uint32_t height) {
    if (!renderer_present || !descriptor_present) return false;
    if (version == Version::legacy_404) return settings_parameter_present;
    return modern(version) && extent_readable &&
        width != 0 && height != 0;
}

} // namespace w3vr::scene_factory
