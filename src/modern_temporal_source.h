#pragma once
#include "modern_factory_clock.h"
#include <bit>
#include <cstring>
#include <utility>

namespace w3vr::temporal_source {
using Prefix=std::array<std::uint8_t,engine_camera::source_prefix_bytes>;
inline constexpr std::uintptr_t wrapper_return=0x02291BF9;
inline constexpr std::array<std::uint8_t,16> builder_prefix{
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55};
inline constexpr std::array<std::uint8_t,5> wrapper_call{0xE8,0x07,0xFA,0xFF,0xFF};
inline bool same_float(float a,float b) noexcept {
    return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);
}
struct Candidate {
    const engine_camera::TemporalContract* contract{};
    std::uintptr_t descriptor{},owner{},director{},history{},position{},rotation{};
    std::uint32_t thread{};
    factory_clock::Sample clock{};
    Prefix prefix{};
    float time{},fov{},aspect{},near_plane{},far_plane{},scale{};
};
inline bool accepted(const Candidate& c) noexcept {
    std::uintptr_t history{},position{},rotation{};
    if(c.contract!=&engine_camera::remastered_1048522 || !c.thread ||
        !c.descriptor || (c.descriptor&7) || !c.owner || (c.owner&7) || !c.director || (c.director&7) ||
        !factory_clock::offset(c.director,0x6D0,history) ||
        !factory_clock::offset(c.director,0xE0,position) ||
        !factory_clock::offset(position,0x10,rotation) || c.history!=history ||
        c.position!=position || c.rotation!=rotation || !same_float(c.time,c.clock.time))return false;
    factory_clock::Values clock{};clock.ticks=c.clock.ticks;clock.scale=c.clock.scale;
    factory_clock::Sample converted{};
    if(!factory_clock::sample_from(clock,converted) || !same_float(converted.time,c.time))return false;
    engine_camera::TemporalInputs inputs{};
    return engine_camera::read_temporal_inputs(c.prefix,c.time,inputs) &&
        same_float(c.fov,inputs.fov) && same_float(c.aspect,inputs.aspect) &&
        same_float(c.near_plane,inputs.near_plane) && same_float(c.far_plane,inputs.far_plane) &&
        same_float(c.scale,inputs.projection_scale);
}
// Compare only native-defined fields. The eleven padding bytes belong to the
// receiving camera and are not identity or history data.
inline bool record_equal(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b) noexcept {
    if(a.size()<engine_camera::record_bytes || b.size()<engine_camera::record_bytes)return false;
    for(const auto [offset,bytes]:std::array<std::pair<std::size_t,std::size_t>,4>{{{0,1},{4,8},{0x10,0x1C},{0x30,0x80}}})
        if(std::memcmp(a.data()+offset,b.data()+offset,bytes))return false;
    return true;
}
// Own copied values within ONE synchronous producer. The temporary record is
// proof of THIS observed builder result, never a previous-frame record cache.
// This does not construct histories, own native objects or publish GPU data.
class Captured {
    Candidate candidate_{};
    engine_camera::TemporalRecord record_{};
    bool attempted_{},valid_{};
public:
    Captured()=default;
    Captured(const Captured&)=delete;
    Captured& operator=(const Captured&)=delete;
    Captured(Captured&&)=delete;
    Captured& operator=(Captured&&)=delete;
    bool capture(const Candidate& c,const engine_camera::TemporalRecord& record,
        std::uintptr_t result,bool still_current) noexcept {
        if(attempted_){valid_=false;return false;}attempted_=true;
        float time{};std::memcpy(&time,record.data()+4,sizeof(time));
        if(!still_current || !accepted(c) || result!=c.history || record[0]!=1 || !same_float(time,c.time))return false;
        candidate_=c;record_=record;valid_=true;return true;
    }
    bool matches(const engine_camera::TemporalContract* contract,std::uintptr_t descriptor,
        std::uintptr_t owner,std::uint32_t thread,const factory_clock::Sample& clock,
        std::span<const std::uint8_t> native_previous,bool still_current) noexcept {
        const bool accepted=valid_ && still_current && contract==candidate_.contract && descriptor==candidate_.descriptor &&
            owner==candidate_.owner && thread==candidate_.thread && clock.ticks==candidate_.clock.ticks &&
            clock.scale==candidate_.clock.scale && same_float(clock.time,candidate_.time) &&
            record_equal(native_previous,record_);
        if(!accepted)revoke();return accepted;
    }
    bool previous_inputs(Prefix& out) const noexcept {if(!valid_)return false;out=candidate_.prefix;return true;}
    void revoke() noexcept {valid_=false;attempted_=true;}
};
} // namespace w3vr::temporal_source
