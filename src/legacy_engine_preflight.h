#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace w3vr {

// Upstream uses these callbacks plus many other fixed 4.04 addresses. This
// probe only rejects an incompatible layout; passing it is not proof that all
// engine hooks or any gameplay are supported on a different executable.
inline constexpr uint32_t legacy_head_callback_rva = 0x01C4B550;
inline constexpr uint32_t legacy_matrix_callback_rva = 0x0152E600;
inline constexpr std::array<uint8_t, 26> legacy_head_callback_entry = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0xFF, 0x42,
    0x30, 0x48, 0x81, 0xC1, 0x00, 0x02, 0x00, 0x00, 0x49,
    0x8B, 0xD8, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x38};
inline constexpr std::array<uint8_t, 27> legacy_matrix_callback_entry = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89,
    0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55, 0x48, 0x8D,
    0x68, 0xA1, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00};

inline bool legacy_engine_layout_matches(std::span<const uint8_t> mapped_image) {
    auto matches = [&](uint32_t rva, const auto& entry) {
        return rva <= mapped_image.size() &&
            entry.size() <= mapped_image.size() - rva &&
            std::equal(entry.begin(), entry.end(), mapped_image.begin() + rva);
    };
    return matches(legacy_head_callback_rva, legacy_head_callback_entry) &&
        matches(legacy_matrix_callback_rva, legacy_matrix_callback_entry);
}

} // namespace w3vr
