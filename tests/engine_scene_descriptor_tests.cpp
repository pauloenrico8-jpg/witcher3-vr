#include "engine_scene_descriptor.h"
#include <array>
#include <cstdlib>
#include <iostream>

namespace descriptor = w3vr::scene_descriptor;
using Version = w3vr::scene_factory::Version;

void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    alignas(16) std::array<std::uint8_t, 0xF760> bytes{};
    const auto legacy = descriptor::copy_policy(Version::legacy_404, false);
    const auto legacy_taau = descriptor::copy_policy(Version::legacy_404, true);
    const auto current = descriptor::copy_policy(Version::remastered_500c, false);
    const auto current_taau = descriptor::copy_policy(Version::remastered_500c, true);
    const auto unknown = descriptor::copy_policy(Version::unknown, false);
    require(legacy.requested_bytes == 0xC000 && legacy_taau.requested_bytes == 0xBD00,
        "Legacy clone policy changed");
    require(current.requested_bytes == 0xF750 && current_taau.requested_bytes == 0xF750,
        "Remastered tail would be truncated by a legacy/TAAU prefix");
    require(!descriptor::copy_accepted(Version::unknown, unknown, bytes, 0),
        "Unknown layout accepted");
    for (std::size_t copied : {0xB000u, 0xBCFFu, 0xBD00u, 0xC000u})
        require(descriptor::copy_accepted(Version::legacy_404, legacy, bytes, copied),
            "Legacy bounded prefix no longer accepted");
    require(!descriptor::copy_accepted(Version::legacy_404, legacy_taau, bytes, 0xC000),
        "Copy larger than requested accepted");
    require(!descriptor::copy_accepted(Version::legacy_404, legacy, bytes, 0xAFFF),
        "Insufficient legacy prefix accepted");
    for (std::size_t copied : {0u, 0xB000u, 0xBD00u, 0xC000u, 0xF748u, 0xF749u, 0xF74Fu})
        require(!descriptor::copy_accepted(Version::remastered_500c, current, bytes, copied),
            "Partial Remastered copy was accepted as a full scene");
    bytes[0xF748] = 0xA5; // fixed tail must be present; its value is not a boolean assumption
    for (std::uint32_t count = 0; count <= 4; ++count) {
        std::memcpy(bytes.data() + 0xECA0, &count, sizeof(count));
        require(descriptor::copy_accepted(Version::remastered_500c, current, bytes, 0xF750),
            "Bounded inline array rejected");
        require(bytes[0xF748] == 0xA5, "Validation changed the borrowed snapshot");
    }
    for (std::uint32_t count : {5u, 0xFFFFFFFFu}) {
        std::memcpy(bytes.data() + 0xECA0, &count, sizeof(count));
        require(!descriptor::copy_accepted(Version::remastered_500c, current, bytes, 0xF750),
            "Inline array could read beyond its descriptor block");
    }
    const std::uint32_t zero = 0;
    std::memcpy(bytes.data() + 0xECA0, &zero, sizeof(zero));
    require(!descriptor::copy_accepted(Version::remastered_500c, current,
        std::span<const std::uint8_t>{bytes.data(), 0xC000}, 0xF750),
        "Allocated buffer shorter than claimed copy accepted");
    require(!descriptor::copy_accepted(Version::remastered_500c, current,
        std::span<const std::uint8_t>{bytes.data() + 1, 0xF750}, 0xF750),
        "Unaligned borrowed scene accepted");
    require(!descriptor::copy_accepted(Version::remastered_500c, legacy, bytes, 0xC000),
        "Wrong-version copy policy accepted");
    std::cout << "Borrowed scene bounds passed; no native game copy was executed.\n";
}
