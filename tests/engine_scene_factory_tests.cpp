#include "engine_scene_factory.h"
#include <cstdlib>
#include <iostream>

namespace factory = w3vr::scene_factory;
namespace camera = w3vr::engine_camera;
int checks{};

void require(bool value, const char* message) {
    ++checks; if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    require(factory::inputs_ready(factory::Version::legacy_404,
        true, true, true, false, 0, 0), "Legacy admission changed");
    require(!factory::inputs_ready(factory::Version::legacy_404,
        true, false, true, true, 1920, 1080), "Legacy accepted missing settings");
    for (auto modern_version : {factory::Version::remastered_500c, factory::Version::remastered_1048522})
    for (bool settings : {false, true}) {
        require(factory::inputs_ready(modern_version,
            true, settings, true, true, 1920, 1080),
            "Remastered rejected a valid descriptor with optional settings");
        require(!factory::inputs_ready(modern_version,
            true, settings, true, false, 1920, 1080), "Unreadable extent accepted");
        require(!factory::inputs_ready(modern_version,
            true, settings, true, true, 0, 1080), "Zero width accepted");
        require(!factory::inputs_ready(modern_version,
            true, settings, true, true, 1920, 0), "Zero height accepted");
    }
    for (auto version : {factory::Version::unknown, factory::Version::legacy_404,
            factory::Version::remastered_500c, factory::Version::remastered_1048522}) {
        require(!factory::inputs_ready(version, false, true, true, true, 1920, 1080),
            "Missing renderer accepted");
        require(!factory::inputs_ready(version, true, true, false, true, 1920, 1080),
            "Missing descriptor accepted");
    }
    require(!factory::inputs_ready(factory::Version::unknown,
        true, true, true, true, 1920, 1080), "Unknown contract accepted");
    require(factory::factory_rva(factory::Version::unknown) == 0,
        "Unknown contract resolved an executable address");
    require(factory::factory_rva(factory::Version::legacy_404)==0x016214C0,"legacy factory address");
    require(factory::factory_rva(factory::Version::remastered_500c)==0x01B80550,"older modern factory address");
    require(factory::factory_rva(factory::Version::remastered_1048522)==0x01B86E10,"current factory address");
    for (const auto* contract:{&camera::legacy_404,&camera::remastered_500c,&camera::remastered_1048522}) {
        require(factory::selected_version(contract)!=factory::Version::unknown,"known contract selects version");
        auto copy=*contract;
        require(factory::selected_version(&copy)==factory::Version::unknown,"copied contract rejected");
    }
    camera::TemporalContract unknown{};
    require(factory::selected_version(nullptr)==factory::Version::unknown &&
        factory::selected_version(&unknown)==factory::Version::unknown,"unknown or null contract rejected");
    const auto invalid=static_cast<factory::Version>(99);
    require(!factory::modern(invalid) && factory::factory_rva(invalid)==0 &&
        !factory::inputs_ready(invalid,true,true,true,true,1920,1080),"invalid enum rejected");
    std::cout << "Factory input profiles: " << checks << " CPU checks; no game execution.\n";
}
