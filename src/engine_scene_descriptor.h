#pragma once

#include "engine_scene_factory.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace w3vr::scene_descriptor {

struct CopyPolicy {
    std::size_t storage_bytes{};
    std::size_t requested_bytes{};
    std::size_t minimum_bytes{};
    bool exact_copy{};
};

// This is a borrowed byte snapshot during the original producer's lifetime,
// not a constructed native object. Never call a native destructor on it or
// retain its pointers as owned resources. The native factory copies/retains
// its own resources synchronously before this borrowed input is discarded.
inline constexpr CopyPolicy copy_policy(scene_factory::Version version,
                                        bool asymmetric_taau) {
    if (version == scene_factory::Version::legacy_404)
        return {0xC000, asymmetric_taau ? 0xBD00u : 0xC000u, 0xB000, false};
    if (version == scene_factory::Version::remastered_500c)
        return {0xF750, 0xF750, 0xF750, true};
    return {};
}

// 5.00c: last fixed byte copied by 00324430 is +F748; the descriptor region
// in the two observed frame constructors ends at +F750 (16-byte alignment).
// Its inline array at EC20+80 has data at EC20+90, 60-byte elements, with
// the next fixed field at EC20+210. At most four elements fit. Reject an
// inconsistent count before the native copy can read outside this region.
inline bool copy_accepted(scene_factory::Version version,
    const CopyPolicy& policy, std::span<const std::uint8_t> snapshot,
    std::size_t copied_bytes) {
    if (policy.storage_bytes == 0 || snapshot.size() < policy.storage_bytes ||
        copied_bytes > policy.requested_bytes || copied_bytes < policy.minimum_bytes)
        return false;
    if (version == scene_factory::Version::legacy_404)
        return !policy.exact_copy && policy.storage_bytes == 0xC000 &&
            (policy.requested_bytes == 0xC000 || policy.requested_bytes == 0xBD00) &&
            policy.minimum_bytes == 0xB000;
    if (version != scene_factory::Version::remastered_500c || !policy.exact_copy ||
        policy.storage_bytes != 0xF750 || policy.requested_bytes != 0xF750 ||
        policy.minimum_bytes != 0xF750 || copied_bytes != policy.requested_bytes ||
        reinterpret_cast<std::uintptr_t>(snapshot.data()) % 16 != 0)
        return false;
    std::uint32_t inline_count{};
    std::memcpy(&inline_count, snapshot.data() + 0xECA0, sizeof(inline_count));
    return inline_count <= 4;
}

} // namespace w3vr::scene_descriptor
