#pragma once

#include "engine_camera_temporal.h"
#include <cstdint>

namespace w3vr::frame_submission {

// Examined in Remastered 5.00c SHA-256
// 9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51.
// These describe a native command route, not overall engine compatibility.
inline constexpr std::uint32_t command_allocator_rva = 0x00395D80;
inline constexpr std::uint32_t command_constructor_rva = 0x02297670;
inline constexpr std::uint32_t command_dispatch_rva = 0x022925E0;
inline constexpr std::uint32_t unavailable_command_rva = 0x05C88BF0;
inline constexpr std::uint32_t renderer_global_rva = 0x05A51950;
inline constexpr std::uint32_t renderer_vtable_rva = 0x036E03C8;

// Exact subsystem selection, not executable admission or Native ownership.
struct Profile {
    std::uint32_t allocator,constructor,dispatch,unavailable,renderer_global,renderer_vtable;
};
inline constexpr Profile remastered_500c{command_allocator_rva,command_constructor_rva,
    command_dispatch_rva,unavailable_command_rva,renderer_global_rva,renderer_vtable_rva};
inline constexpr Profile remastered_1048522{0x003945F0,0x0229FD80,0x0229ACF0,
    0x05C92B30,0x05A5B948,0x036EB088};
inline bool known(const Profile* profile){return profile==&remastered_500c || profile==&remastered_1048522;}
inline const Profile* selected(const engine_camera::TemporalContract* contract){
    if(contract==&engine_camera::remastered_500c)return &remastered_500c;
    if(contract==&engine_camera::remastered_1048522)return &remastered_1048522;
    return nullptr;
}

#if defined(_MSC_VER)
using AllocateCommand = void*(__fastcall*)();
using ConstructCommand = void*(__fastcall*)(void*, void*, void*);
// OWN adapter: confirm publication; do not cast the Native void procedure to this type.
using DispatchCommand = bool(__fastcall*)(void*, void*);
using ReleaseFrame = void(__fastcall*)(void*);
#else
using AllocateCommand = void*(*)();
using ConstructCommand = void*(*)(void*, void*, void*);
using DispatchCommand = bool(*)(void*, void*);
using ReleaseFrame = void(*)(void*);
#endif

struct NativeCommandRoute {
    AllocateCommand allocate{};
    ConstructCommand construct{};
    DispatchCommand dispatch{};
    void* unavailable_command{};
    bool renderer_ready{};
    const Profile* profile{};
    // Third constructor argument. Null for the no-world producer; the WORLD
    // producer's exact borrowed scene [[[game+F0]+8]+5440], which the native
    // constructor add-refs and the native command destructor releases.
    void* auxiliary{};
};

enum class Result { dispatched, invalid_frame, unavailable, allocation_failed, publication_unconfirmed };

// Consumes exactly the caller's owned frame reference if frame/release are
// valid, including normal pre-dispatch failure. The native constructor acquires
// the command's separate reference; the native command destructor releases it.
// Never allocate this command with C++ new: dispatch uses its native preheader.
// Construction's return value is unused: the examined native function always
// returns its destination, and dispatch targets that same native allocation.
// The OWN dispatch adapter must confirm the publication instruction, rather
// than treating a returned Native void call as success. No queue execution,
// GPU completion or OpenXR image follows from this CPU publication receipt.
inline Result submit_owned_frame(const NativeCommandRoute& route, void* frame,
    ReleaseFrame release) {
    if (frame == nullptr || release == nullptr) return Result::invalid_frame;
    if (!known(route.profile) || !route.renderer_ready || route.allocate == nullptr ||
        route.construct == nullptr || route.dispatch == nullptr ||
        route.unavailable_command == nullptr) {
        release(frame);
        return Result::unavailable;
    }
    void* command = route.allocate();
    if (command == nullptr || command == route.unavailable_command) {
        release(frame);
        return Result::allocation_failed;
    }
    (void)route.construct(command, frame, route.auxiliary);
    const bool confirmed=route.dispatch(command,frame);
    // Native construction retained a separate reference. Uncertain command
    // storage is never freed/retried here; release only our factory reference.
    release(frame);
    return confirmed ? Result::dispatched : Result::publication_unconfirmed;
}

} // namespace w3vr::frame_submission
