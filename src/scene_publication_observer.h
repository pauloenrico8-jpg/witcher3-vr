#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
namespace w3vr::publication_observer {
// OWN observer transport, never a Native C++ return declaration. Only the
// examined register-input procedure is eligible; there are no stack inputs.
// CPU identities are borrowed within one synchronous call, never GPU leases.
struct alignas(16) Frame {
    std::uintptr_t caller{};
    std::array<std::uint64_t,7> entry_integer{}; // RAX,RCX,RDX,R8,R9,R10,R11
    std::array<std::uint64_t,7> result_integer{};
    std::uintptr_t owner{};
    std::uint64_t serial{},pair{},generation{};
};
static_assert(offsetof(Frame,entry_integer)==8 && offsetof(Frame,result_integer)==0x40);
static_assert(offsetof(Frame,owner)==0x78 && offsetof(Frame,serial)==0x80 &&
    offsetof(Frame,pair)==0x88 && offsetof(Frame,generation)==0x90 && sizeof(Frame)==0xA0);
inline bool available()noexcept{return IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)!=FALSE;}
}
extern "C" void* w3vr_scene_publication_original;
extern "C" void w3vr_scene_publication_entry();
