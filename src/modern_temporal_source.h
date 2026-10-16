#pragma once
#include "modern_factory_clock.h"
#include <bit>
#include <cstring>
#include <utility>

namespace w3vr::temporal_source {
using Prefix=std::array<std::uint8_t,engine_camera::source_prefix_bytes>;
inline constexpr std::uintptr_t wrapper_return=0x02291BF9;
inline constexpr std::uintptr_t producer_update_return=0x022BE8AC;
enum class SourceSite { descriptor_getter,producer_update };
inline constexpr std::uintptr_t producer_arguments_rva=0x022BE842;
inline constexpr std::array<std::uint8_t,106> producer_arguments_pin{
    0xC5,0xFA,0x10,0x53,0x34,0x48,0x8B,0x05,0xDA,0xD0,0x79,0x03,0x4C,0x8D,0x4B,0x10,0xC5,0xF8,0x57,0xC0,0x48,0x8D,0x8E,0xD0,0x06,0x00,0x00,0x4C,0x8B,0xC3,0xC4,0xE1,0xFB,0x2A,0x80,0x40,0x01,0x00,0x00,0xC5,0xFB,0x5E,0x0D,0xE7,0xF1,0x79,0x03,0xC5,0xFA,0x10,0x43,0x2C,0xC5,0xFA,0x11,0x44,0x24,0x40,0xC5,0xFA,0x10,0x43,0x30,0xC5,0xFA,0x11,0x54,0x24,0x38,0xC5,0xFA,0x10,0x53,0x28,0xC5,0xFA,0x11,0x44,0x24,0x30,0xC5,0xFA,0x10,0x43,0x1C,0xC5,0xFA,0x11,0x54,0x24,0x28,0xC5,0xF3,0x5A,0xC9,0xC5,0xFA,0x11,0x44,0x24,0x20,0xE8,0x54,0x2D,0xFD,0xFF};
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
    SourceSite site{SourceSite::descriptor_getter};
    std::uintptr_t module{},caller{}; // borrowed only within the synchronous producer

};
inline bool accepted(const Candidate& c) noexcept {
    std::uintptr_t history{},position{},rotation{};
    const bool getter=c.site==SourceSite::descriptor_getter && c.descriptor && !(c.descriptor&7);
    const bool update=c.site==SourceSite::producer_update && !c.descriptor && c.module && !(c.module&0xFFF) &&
        c.module<=UINTPTR_MAX-producer_update_return && c.caller==c.module+producer_update_return;
    if(c.contract!=&engine_camera::remastered_1048522 || !c.thread ||
        (!getter && !update) || !c.owner || (c.owner&7) || !c.director || (c.director&7) ||
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
inline constexpr std::uintptr_t reset_return=0x02384AE4;
inline constexpr std::uintptr_t producer_reset_return=0x022BE8C1;
inline constexpr std::uintptr_t producer_reset_setup_rva=0x022BE8AE;
inline constexpr std::array<std::uint8_t,19> producer_reset_setup_pin{0x48,0x8D,0x8E,0xD0,0x06,0x00,0x00,0x48,0x8D,0x15,0xA4,0x2C,0xE9,0x02,0xE8,0xDF,0x50,0x09,0xFE};
inline constexpr std::uintptr_t reset_source=0x05151560;
struct ResetCandidate {
    const engine_camera::TemporalContract* contract{};
    std::uintptr_t module{},descriptor{},owner{},director{},history{},source{};
    std::uint32_t thread{};
    factory_clock::Sample clock{}; // Producer identity only, NOT NativeBuilder time admission.
    SourceSite site{SourceSite::descriptor_getter};
    std::uintptr_t caller{};
};
inline bool accepted(const ResetCandidate& c) noexcept {
    std::uintptr_t history{},source{};
    factory_clock::Values v{};v.ticks=c.clock.ticks;v.scale=c.clock.scale;
    factory_clock::Sample s{};
    const bool getter=c.site==SourceSite::descriptor_getter && c.descriptor && !(c.descriptor&7);
    const bool update=c.site==SourceSite::producer_update && !c.descriptor && c.module &&
        c.module<=UINTPTR_MAX-producer_reset_return && c.caller==c.module+producer_reset_return;
    return c.contract==&engine_camera::remastered_1048522 && c.module && !(c.module&0xFFF) &&
        (getter || update) && c.owner && !(c.owner&7) && c.director && !(c.director&7) &&
        c.thread && c.director<=UINTPTR_MAX-0x7FB && c.module<=UINTPTR_MAX-reset_source-engine_camera::record_bytes &&
        factory_clock::offset(c.director,0x6D0,history) && c.history==history &&
        factory_clock::offset(c.module,reset_source,source) && c.source==source &&
        factory_clock::sample_from(v,s) && same_float(s.time,c.clock.time);
}
enum class Kind { missing,built,reset };
struct Observation {
    Kind kind{Kind::missing};
    factory_clock::Sample clock{}; // Reset clock is producer identity only.
    Prefix previous{};
    engine_camera::TemporalRecord reset_record{};
};
class Captured {
    Candidate candidate_{};
    engine_camera::TemporalRecord record_{};
    bool attempted_{},valid_{},reset_{},bound_{};
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
        candidate_=c;record_=record;valid_=true;bound_=c.site==SourceSite::descriptor_getter;return true;
    }
    bool capture_reset(const ResetCandidate& c,const engine_camera::TemporalRecord& canonical,
        const engine_camera::TemporalRecord& copied,std::uintptr_t result,bool still_current) noexcept {
        if(attempted_){revoke();return false;}attempted_=true;
        if(!still_current || !accepted(c) || result!=c.history || canonical[0]!=0 ||
            !record_equal(canonical,copied))return false;
        candidate_.contract=c.contract;candidate_.descriptor=c.descriptor;candidate_.owner=c.owner;
        candidate_.thread=c.thread;candidate_.clock=c.clock;candidate_.time=c.clock.time;
        candidate_.site=c.site;candidate_.module=c.module;candidate_.caller=c.caller;
        record_=copied;valid_=true;reset_=true;bound_=c.site==SourceSite::descriptor_getter;return true;
    }
    bool reset_observed() const noexcept {return valid_ && reset_;}
    // One observation per producer. A later site must not revoke an earlier one.
    bool attempted() const noexcept {return attempted_;}
    bool matches(const engine_camera::TemporalContract* contract,std::uintptr_t descriptor,
        std::uintptr_t owner,std::uint32_t thread,const factory_clock::Sample& clock,
        std::span<const std::uint8_t> native_previous,bool still_current) noexcept {
        const bool descriptor_matches=candidate_.site==SourceSite::producer_update && !bound_
            ? descriptor && !(descriptor&7) : descriptor==candidate_.descriptor;
        const bool accepted=valid_ && still_current && contract==candidate_.contract && descriptor_matches &&
            owner==candidate_.owner && thread==candidate_.thread && clock.ticks==candidate_.clock.ticks &&
            clock.scale==candidate_.clock.scale && same_float(clock.time,candidate_.time) &&
            record_equal(native_previous,record_);
        if(!accepted){revoke();return false;}
        // Bind the first exact real descriptor only AFTER clock and copied record agree.
        // A later descriptor or failed attempt cannot retarget/rearm this receipt.
        if(!bound_){candidate_.descriptor=descriptor;bound_=true;}return true;
    }
    bool previous_inputs(Prefix& out) const noexcept {if(!valid_ || !bound_ || reset_)return false;out=candidate_.prefix;return true;}
    bool built_record(engine_camera::TemporalRecord& out) const noexcept {
        if(!valid_ || !bound_ || reset_)return false;out=record_;return true;
    }
    // Copied values only. No Game pointer or constructor/lifetime permission.
    bool observation(Observation& out) const noexcept {
        if(!valid_ || !bound_)return false;
        Observation staged{};staged.clock=candidate_.clock;
        if(reset_){staged.kind=Kind::reset;staged.reset_record=record_;}
        else{staged.kind=Kind::built;staged.previous=candidate_.prefix;}
        out=staged;return true;
    }
    void revoke() noexcept {valid_=false;attempted_=true;}
};
} // namespace w3vr::temporal_source
