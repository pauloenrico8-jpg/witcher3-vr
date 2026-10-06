#include "engine_scene_factory.h"
#include <cstdlib>
#include <iostream>

namespace factory = w3vr::scene_factory;

void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    require(factory::inputs_ready(factory::Version::legacy_404,
        true, true, true, false, 0, 0), "Legacy admission changed");
    require(!factory::inputs_ready(factory::Version::legacy_404,
        true, false, true, true, 1920, 1080), "Legacy accepted missing settings");
    for (bool settings : {false, true}) {
        require(factory::inputs_ready(factory::Version::remastered_500c,
            true, settings, true, true, 1920, 1080),
            "Remastered rejected a valid descriptor with optional settings");
        require(!factory::inputs_ready(factory::Version::remastered_500c,
            true, settings, true, false, 1920, 1080), "Unreadable extent accepted");
        require(!factory::inputs_ready(factory::Version::remastered_500c,
            true, settings, true, true, 0, 1080), "Zero width accepted");
        require(!factory::inputs_ready(factory::Version::remastered_500c,
            true, settings, true, true, 1920, 0), "Zero height accepted");
    }
    for (auto version : {factory::Version::unknown, factory::Version::legacy_404,
            factory::Version::remastered_500c}) {
        require(!factory::inputs_ready(version, false, true, true, true, 1920, 1080),
            "Missing renderer accepted");
        require(!factory::inputs_ready(version, true, true, false, true, 1920, 1080),
            "Missing descriptor accepted");
    }
    require(!factory::inputs_ready(factory::Version::unknown,
        true, true, true, true, 1920, 1080), "Unknown contract accepted");
    require(factory::factory_rva(factory::Version::unknown) == 0,
        "Unknown contract resolved an executable address");
    std::cout << "Legacy and Remastered factory admission passed without game execution.\n";
}
