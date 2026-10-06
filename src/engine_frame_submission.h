#pragma once

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

#if defined(_MSC_VER)
using AllocateCommand = void*(__fastcall*)();
using ConstructCommand = void*(__fastcall*)(void*, void*, void*);
using DispatchCommand = void(__fastcall*)(void*);
using ReleaseFrame = void(__fastcall*)(void*);
#else
using AllocateCommand = void*(*)();
using ConstructCommand = void*(*)(void*, void*, void*);
using DispatchCommand = void(*)(void*);
using ReleaseFrame = void(*)(void*);
#endif

struct NativeCommandRoute {
    AllocateCommand allocate{};
    ConstructCommand construct{};
    DispatchCommand dispatch{};
    void* unavailable_command{};
    bool renderer_ready{};
};

enum class Result { dispatched, invalid_frame, unavailable, allocation_failed };

// Consumes exactly the caller's owned frame reference if frame/release are
// valid, including normal pre-dispatch failure. The native constructor acquires
// the command's separate reference; the native command destructor releases it.
// Never allocate this command with C++ new: dispatch uses its native preheader.
// Construction's return value is unused: the examined native function always
// returns its destination, and dispatch targets that same native allocation.
// A returned "dispatched" means only that the native dispatch call returned.
// It does not prove queue execution, GPU completion or an OpenXR eye image.
inline Result submit_owned_frame(const NativeCommandRoute& route, void* frame,
    ReleaseFrame release) {
    if (frame == nullptr || release == nullptr) return Result::invalid_frame;
    if (!route.renderer_ready || route.allocate == nullptr ||
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
    (void)route.construct(command, frame, nullptr);
    route.dispatch(command);
    release(frame);
    return Result::dispatched;
}

} // namespace w3vr::frame_submission
