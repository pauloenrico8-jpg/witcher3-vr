#pragma once
#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace w3vr::producer_bridge {
// Win64 register-only call transport. This does NOT declare the Native return
// type. Byte arrays preserve NaN payloads, upper AVX lanes and scalar bits.
// No stack argument beyond the first four register slots is transported.
struct alignas(16) Frame {
    std::array<std::uint64_t,4> entry_integer;
    std::array<std::array<std::byte,32>,4> entry_vector;
    std::array<std::uint64_t,2> return_integer;
    std::array<std::array<std::byte,32>,4> return_vector;
};
static_assert(sizeof(void*)==8 && sizeof(Frame)==0x130 && alignof(Frame)==16);
static_assert(offsetof(Frame,entry_integer)==0 && offsetof(Frame,entry_vector)==0x20 &&
    offsetof(Frame,return_integer)==0xA0 && offsetof(Frame,return_vector)==0xB0);
inline bool available() noexcept {
    // The examined Game producer already executes AVX. Keep admission closed
    // on a machine/OS that cannot preserve the complete observed AVX lanes.
    return IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)!=FALSE;
}
}
// Address-only Native entry: C++ never calls it with a guessed return type.
extern "C" void w3vr_producer_entry();
// This helper has an OWN, defined ABI; its assembly calls the opaque original.
extern "C" void w3vr_invoke_producer_original(void* original,w3vr::producer_bridge::Frame* frame);
