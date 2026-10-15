#pragma once
#include "engine_frame_submission.h"
#include <array>
#include <cstring>
#include <limits>
#include <span>
namespace w3vr::scene_publication {
// This is the known one-argument Native procedure, with no C++ return value.
#if defined(_MSC_VER)
using Original=void(__fastcall*)(void*);
#else
using Original=void(*)(void*);
#endif
inline constexpr std::uint64_t unpublished=0x4A545330;
inline constexpr std::size_t code_bytes=85;
// Complete instruction shape; the two version-specific RIP operands are
// resolved separately below. Never apply one address delta to both targets.
inline constexpr std::array<std::uint8_t,code_bytes> shape{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0x0D,0x00,0x00,0x00,0x00,0x48,0x85,0xC9,0x74,0x3A,0x48,0x8D,0x05,0x00,0x00,0x00,0x00,0x48,0x3B,0xD8,0x74,0x2E,0x48,0x8B,0x01,0xFF,0x90,0x80,0x00,0x00,0x00,0x90,0xF0,0x83,0x0C,0x24,0x00,0xF0,0x83,0x4B,0xF4,0x00,0x90,0xF0,0x83,0x0C,0x24,0x00,0x90,0xF0,0x83,0x0C,0x24,0x00,0xB8,0x43,0x45,0x58,0x45,0x87,0x43,0xF0,0x90,0xF0,0x83,0x0C,0x24,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3};
inline bool add(std::uintptr_t base,std::uintptr_t offset,std::uintptr_t& out){
    if(!base || offset>UINTPTR_MAX-base)return false;out=base+offset;return true;
}
inline bool rip(std::uintptr_t next,std::int32_t displacement,std::uintptr_t& out){
    if(displacement>=0)return add(next,static_cast<std::uintptr_t>(displacement),out);
    const auto back=static_cast<std::uint64_t>(-static_cast<std::int64_t>(displacement));
    if(next<back)return false;out=next-back;return true;
}
inline bool matches(const frame_submission::Profile* profile,std::uintptr_t module,std::span<const std::uint8_t> bytes){
    if(!frame_submission::known(profile) || bytes.size()!=code_bytes)return false;
    std::uintptr_t entry{},global{},sentinel{},next{},target{};
    if(!add(module,profile->dispatch,entry) || !add(module,profile->renderer_global,global) ||
        !add(module,profile->unavailable,sentinel) || sentinel==unpublished)return false;
    for(std::size_t i=0;i<code_bytes;++i){
        if((i>=12&&i<16)||(i>=24&&i<28))continue;
        if(bytes[i]!=shape[i])return false;
    }
    std::int32_t displacement{};std::memcpy(&displacement,bytes.data()+12,4);
    if(!add(entry,16,next) || !rip(next,displacement,target) || target!=global)return false;
    std::memcpy(&displacement,bytes.data()+24,4);
    return add(entry,28,next) && rip(next,displacement,target) && target==sentinel;
}
// The OWN bridge seeds RAX=0, invokes Original once, and captures raw RAX.
// In the verified procedure only XCHG supplies the unpublished marker there.
// This receipt proves CPU publication, not execution, GPU completion or pixels.
inline bool published(std::uint64_t raw_rax){return raw_rax==unpublished;}
}
extern "C" std::uint64_t w3vr_invoke_scene_publication(w3vr::scene_publication::Original,void* command) noexcept(false);
