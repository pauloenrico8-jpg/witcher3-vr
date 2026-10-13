#pragma once
#include "engine_camera_temporal.h"
#include <limits>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace w3vr::camera_defaults {
using Words=std::array<std::uint32_t,16>;
inline constexpr Words identity_words{0x3F800000,0,0,0,0,0x3F800000,0,0,0,0,0x3F800000,0,0,0,0,0x3F800000};
struct LoadPin {std::uintptr_t rva;std::array<std::uint8_t,8> bytes;};
struct Profile {std::uintptr_t image_bytes,matrix;std::array<LoadPin,2> loads;};
// Required reader sites and expected values for ONE independently examined
// build. The observer never runs an initializer or writes Native globals.
inline constexpr Profile current{0x06402000,0x05C78BF0,{{
    {0x0229173E,{0xC5,0xFC,0x10,0x05,0xAA,0x74,0x9E,0x03}},
    {0x0229174C,{0xC5,0xF8,0x28,0x05,0xBC,0x74,0x9E,0x03}}}}};
inline const Profile* selected(const engine_camera::TemporalContract* c) noexcept {
    return c==&engine_camera::remastered_1048522 ? &current : nullptr;
}
// Reader writes only to the supplied OWN temporary and returns read success.
// Two equal observations are NOT synchronization, ABA protection, a module
// lease, Game initialization proof or permission for an extra NativeBuilder.
// Actual host admission additionally requires its existing loaded-clock and
// exact normal caller/source scope; this bounded helper supplies none of those.
template<class Reader>
bool read(const engine_camera::TemporalContract* c,const Profile* p,
    std::uintptr_t module,Reader&& reader,Words& out) {
    if(!p || p!=selected(c) || !module || (module&0xFFF) ||
        module>UINTPTR_MAX-p->image_bytes || p->matrix>=p->image_bytes ||
        sizeof(Words)>p->image_bytes-p->matrix)return false;
    Words first{},second{};
    for(unsigned pass=0;pass<2;++pass){
        for(const auto& pin:p->loads){
            std::array<std::uint8_t,8> bytes{};
            if(pin.rva>=p->image_bytes || bytes.size()>p->image_bytes-pin.rva ||
                !reader(module+pin.rva,bytes.data(),bytes.size()) || bytes!=pin.bytes)return false;
        }
        auto& value=pass ? second : first;
        if(!reader(module+p->matrix,value.data(),sizeof(value)) || value!=identity_words)return false;
    }
    if(first!=second)return false;out=first;return true;
}
#if defined(_MSC_VER) && defined(_WIN32)
// Shared host/test protected byte reader. Tests use reserved OWN pages only.
inline bool guarded_read(std::uintptr_t address,void* out,std::size_t count) noexcept {
    if(!address || !out || !count || address>UINTPTR_MAX-count)return false;
    __try {std::memcpy(out,reinterpret_cast<const void*>(address),count);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
#endif
} // namespace w3vr::camera_defaults
