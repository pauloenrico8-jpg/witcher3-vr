#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
namespace w3vr::temporal_leaf {
// OWN post-call observation frame, not a declaration of a Native C++ ABI.
// The examined two-pointer leaf has no stack inputs. Its caller relies on
// surviving R8. Preserve every volatile GPR, all AVX lanes and incoming flags.
// The bridge also saves/restores post-original MXCSR and x87 state privately;
// that aligned FX image is not exposed as, or appended to, this C++ frame.
struct alignas(16) Frame {
    std::uintptr_t destination{},source{},caller{},reserved{};
    std::array<std::uint64_t,7> integer{}; // RAX,RCX,RDX,R8,R9,R10,R11
    std::uint64_t padding{};
    std::array<std::array<std::byte,32>,16> vector{};
};
static_assert(sizeof(Frame)==0x260 && offsetof(Frame,integer)==0x20 && offsetof(Frame,vector)==0x60);
inline bool available() noexcept {return IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)!=FALSE;}
}
extern "C" void* w3vr_temporal_leaf_original;
extern "C" void w3vr_temporal_leaf_entry();
