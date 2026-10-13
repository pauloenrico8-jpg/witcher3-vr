#pragma once
#include "engine_camera_temporal.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace w3vr::factory_clock {
struct CodePin {std::uintptr_t rva;std::span<const std::uint8_t> bytes;};
inline constexpr std::array<std::uint8_t,3> receiver_pin{0x48,0x8B,0xF9};
inline constexpr std::array<std::uint8_t,37> tick_pin{
    0xE8,0x24,0xE5,0x26,0x00,0xC4,0xC1,0x12,0x5A,0xC5,0xC5,0xFB,0x59,0x0D,0x8F,0x06,0x7E,0x03,
    0xC4,0xE1,0xFB,0x2C,0xC1,0x48,0x01,0x87,0x40,0x01,0x00,0x00,0x48,0x01,0x87,0x48,0x01,0x00,0x00};
inline constexpr std::array<std::uint8_t,9> descriptor_pin{0x48,0x8D,0x4D,0x80,0xE8,0x06,0xE7,0x03,0x00};
inline constexpr std::array<std::uint8_t,4> argument_pin{0x4C,0x8D,0x45,0x80};
inline constexpr std::array<std::uint8_t,9> call_pin{0xFF,0x90,0x08,0x02,0x00,0x00,0x48,0x8B,0xD8};
inline constexpr std::array<std::uint8_t,51> time_pin{
    0x48,0x8B,0x05,0x85,0x6E,0x6D,0x03,0x48,0x8D,0x8B,0xD0,0x06,0x00,0x00,0xC5,0xF8,0x57,0xC0,
    0x4C,0x8D,0x83,0xE0,0x00,0x00,0x00,0xC4,0xE1,0xFB,0x2A,0x80,0x40,0x01,0x00,0x00,
    0xC5,0xFB,0x5E,0x0D,0x92,0x8F,0x6D,0x03,0xC5,0xF3,0x5A,0xC9,0xE8,0xE1,0xD0,0xF0,0xFF};
inline constexpr std::array<std::uint8_t,16> fragment_unwind_pin{0x21,0x00,0x00,0x00,0x0B,0xD6,0x27,0x02,0x48,0xD6,0x27,0x02,0xF4,0xBA,0xF5,0x03};
inline constexpr std::array<std::uint8_t,16> chain1_unwind_pin{0x21,0x00,0x00,0x00,0x62,0xD4,0x27,0x02,0xF6,0xD4,0x27,0x02,0xB8,0xBA,0xF5,0x03};
inline constexpr std::array<std::uint8_t,32> chain2_unwind_pin{0x21,0x23,0x08,0x00,0x23,0x88,0x83,0x0F,0x1A,0x78,0x84,0x0F,0x11,0x68,0x85,0x0F,0x08,0x34,0x11,0x1F,0x99,0xD3,0x27,0x02,0x62,0xD4,0x27,0x02,0xA4,0xBA,0xF5,0x03};
inline constexpr std::array<std::uint8_t,20> chain3_unwind_pin{0x21,0x08,0x02,0x00,0x08,0x64,0x13,0x1F,0xF0,0xD1,0x27,0x02,0x99,0xD3,0x27,0x02,0x8C,0xBA,0xF5,0x03};
inline constexpr std::array<std::uint8_t,24> primary_unwind_pin{0x01,0x3D,0x0A,0x00,0x3D,0xE8,0x7D,0x0F,0x34,0xD8,0x7E,0x0F,0x2B,0xC8,0x7F,0x0F,0x18,0x01,0x0D,0x1F,0x03,0x70,0x02,0x50};
inline constexpr std::array<std::uint8_t,7> viewport_argument_pin{0x48,0x8B,0x97,0x00,0x01,0x00,0x00};
inline constexpr std::array<std::uint8_t,27> camera_receiver_pin{
    0x48,0x8B,0x13,0x48,0x85,0xD2,0x74,0x23,0x48,0x8B,0x4A,0x08,0x48,0x85,0xC9,0x74,0x1A,
    0x48,0x8B,0x01,0x4C,0x8D,0x43,0x10,0xFF,0x50,0x58};
inline constexpr std::array<std::uint8_t,33> camera_director_pin{
    0x48,0x8B,0x81,0x98,0x00,0x00,0x00,0x48,0x85,0xC0,0x74,0x15,0x48,0x8B,0x48,0x08,
    0x48,0x85,0xC9,0x74,0x0C,0x48,0x8B,0x89,0x80,0x54,0x00,0x00,0xE8,0xC8,0x44,0x61,0x00};
struct UnwindNode {std::uint32_t begin,end,data;std::uint8_t slots,flags;};
struct Profile {
    std::uintptr_t factory_return,fragment,clock_global,scale_global;
    std::uint32_t timestamp,image_bytes;
    std::array<std::uintptr_t,3> primary_vtables;
    std::array<std::uintptr_t,3> camera_vtables,camera_getters;
    std::array<CodePin,14> pins;
    std::array<UnwindNode,5> unwind;
};
// Only this normal caller has an independently examined chained-unwind route.
// A matching profile/code span is not a whole-program compatibility gate.
inline constexpr Profile current{
    0x0227DF27,0x0227DDAC,0x05A5B928,0x05A5DA58,0x6AC555A3,0x06402000,
    {0x03953748,0x0382C8D8,0x037B41D0},
    {0x03953688,0x0382CE80,0x037B4110},{0x0227EA50,0x01F4BC70,0x01D70500},
    {{{0x0227D20F,receiver_pin},{0x0227D3B7,tick_pin},{0x0227DED1,descriptor_pin},
      {0x0227DEF1,argument_pin},{0x0227DF21,call_pin},{0x02384A9C,time_pin},
      {0x03F5BB18,fragment_unwind_pin},{0x03F5BAF4,chain1_unwind_pin},{0x03F5BAB8,chain2_unwind_pin},{0x03F5BAA4,chain3_unwind_pin},{0x03F5BA8C,primary_unwind_pin},
      {0x0227DECA,viewport_argument_pin},{0x022BCA66,camera_receiver_pin},{0x01D70537,camera_director_pin}}},
    {{{0x0227DDAC,0x0227E0D0,0x03F5BB18,0,4},{0x0227D60B,0x0227D648,0x03F5BAF4,0,4},
      {0x0227D462,0x0227D4F6,0x03F5BAB8,8,4},{0x0227D399,0x0227D462,0x03F5BAA4,2,4},
      {0x0227D1F0,0x0227D399,0x03F5BA8C,10,0}}}};
inline const Profile* selected(const engine_camera::TemporalContract* contract) noexcept {
    return contract==&engine_camera::remastered_1048522 ? &current : nullptr;
}
inline bool known(const Profile* profile) noexcept {return profile==&current;}
inline bool range(std::uintptr_t start,std::size_t bytes,std::uintptr_t low,std::uintptr_t high) noexcept {
    return bytes && low<high && start>=low && start<high && bytes<=high-start;
}
inline bool offset(std::uintptr_t base,std::uintptr_t rva,std::uintptr_t& out) noexcept {
    if(!base || rva>std::numeric_limits<std::uintptr_t>::max()-base)return false;
    out=base+rva;return true;
}
inline bool table_matches(const Profile* profile,std::uintptr_t module,std::uintptr_t table) noexcept {
    if(!known(profile))return false;
    for(auto rva:profile->primary_vtables){std::uintptr_t wanted{};if(offset(module,rva,wanted) && table==wanted)return true;}
    return false;
}
enum class CallSite {factory,descriptor_constructor};
inline constexpr std::uintptr_t descriptor_constructor_return=0x0227DEDA;
struct Caller {
    std::uintptr_t pc{},module{},fragment{},sp{},rbp{},rdi{},stack_low{},stack_high{};
    std::uint32_t thread{};
};
// Values observed from the ORIGINAL descriptor and its viewport. These are
// borrowed pointers in one CPU scope; no interface calls or reference changes.
struct CameraBinding {
    std::uintptr_t descriptor_viewport{},owner_viewport{},receiver{},table{},getter{};
    bool operator==(const CameraBinding&) const=default;
};
struct Values {std::uintptr_t owner{},table{};std::int64_t ticks{};double scale{};CameraBinding camera{};};
inline bool camera_matches(const Profile* profile,std::uintptr_t module,const Values& values) noexcept {
    if(!known(profile) || !values.owner || (values.owner&7) ||
        !values.camera.descriptor_viewport || (values.camera.descriptor_viewport&7) ||
        values.camera.descriptor_viewport!=values.camera.owner_viewport)return false;
    std::uintptr_t receiver{};
    if(!offset(values.owner,0x58,receiver) || values.camera.receiver!=receiver)return false;
    // Pair each PRIMARY class with its own SECONDARY camera interface and slot11.
    // Primary slot11 is a different method. A copied or mixed class is rejected.
    for(std::size_t i=0;i<profile->primary_vtables.size();++i){
        std::uintptr_t primary{},secondary{},getter{};
        if(offset(module,profile->primary_vtables[i],primary) && values.table==primary &&
            offset(module,profile->camera_vtables[i],secondary) && values.camera.table==secondary &&
            offset(module,profile->camera_getters[i],getter) && values.camera.getter==getter)return true;
    }
    return false;
}
struct Sample {std::int64_t ticks{};double scale{};float time{};};
inline bool caller_matches(const Profile* profile,const Caller& caller,std::uintptr_t descriptor,
    std::uint32_t thread,CallSite site=CallSite::factory) noexcept {
    if(site!=CallSite::factory && site!=CallSite::descriptor_constructor)return false;
    std::uintptr_t pc{},fragment{},image_end{};
    return known(profile) && thread && caller.thread==thread &&
        offset(caller.module,site==CallSite::factory ? profile->factory_return : descriptor_constructor_return,pc) && offset(caller.module,profile->fragment,fragment) &&
        offset(caller.module,profile->image_bytes,image_end) && caller.pc==pc && caller.fragment==fragment &&
        range(caller.sp,8,caller.stack_low,caller.stack_high) && !(caller.sp&7) && !(caller.rbp&7) &&
        caller.rbp>=0x80 && descriptor==caller.rbp-0x80 && descriptor>=caller.sp &&
        range(descriptor,0xF750,caller.stack_low,caller.stack_high) && caller.rdi && !(caller.rdi&7);
}
inline bool sample_from(const Values& values,Sample& out) noexcept {
    if(!std::isfinite(values.scale) || values.scale<=0)return false;
    // Match signed int64 -> double, division in double, then native float.
    const double seconds=static_cast<double>(values.ticks)/values.scale;
    if(!std::isfinite(seconds) || std::abs(seconds)>std::numeric_limits<float>::max())return false;
    const float time=static_cast<float>(seconds);
    if(!std::isfinite(time))return false;
    out={values.ticks,values.scale,time};return true;
}
// Borrowed CURRENT-thread CPU scope. No copied/moved scope, global cache,
// game calls, pointer ownership, GPU publication or history-builder admission.
class BorrowedScope {
    const Profile* profile_{};
    Caller caller_{};
    Values values_{};
    std::uintptr_t descriptor_{};
    Sample sample_{};
    bool attempted_{},active_{};
public:
    BorrowedScope()=default;
    BorrowedScope(const BorrowedScope&)=delete;
    BorrowedScope& operator=(const BorrowedScope&)=delete;
    BorrowedScope(BorrowedScope&&)=delete;
    BorrowedScope& operator=(BorrowedScope&&)=delete;
    bool begin(const engine_camera::TemporalContract* contract,const Profile* profile,
        const Caller& caller,std::uintptr_t descriptor,const Values& values,bool code_matches,
        std::uint32_t thread,CallSite site=CallSite::factory) noexcept {
        if(attempted_){active_=false;return false;}attempted_=true;
        Sample sample{};
        if(!known(profile) || selected(contract)!=profile || !code_matches ||
            !caller_matches(profile,caller,descriptor,thread,site) ||
            values.owner!=caller.rdi || !table_matches(profile,caller.module,values.table) ||
            !camera_matches(profile,caller.module,values) ||
            !sample_from(values,sample))return false;
        profile_=profile;caller_=caller;values_=values;descriptor_=descriptor;sample_=sample;active_=true;return true;
    }
    bool revalidate(const engine_camera::TemporalContract* contract,const Values& values,
        bool code_matches,std::uint32_t thread) noexcept {
        if(!active_ || !code_matches || selected(contract)!=profile_ || thread!=caller_.thread ||
            values.owner!=values_.owner || values.table!=values_.table || values.ticks!=values_.ticks ||
            values.scale!=values_.scale || values.camera!=values_.camera){active_=false;return false;}
        return true;
    }
    bool sample(std::uint32_t thread,Sample& out) const noexcept {
        if(!active_ || thread!=caller_.thread)return false;out=sample_;return true;
    }
    void revoke() noexcept {active_=false;attempted_=true;}
    const Caller& borrowed_caller() const noexcept {return caller_;}
    std::uintptr_t borrowed_descriptor() const noexcept {return descriptor_;}
    const Profile* profile() const noexcept {return active_ ? profile_ : nullptr;}
};
} // namespace w3vr::factory_clock
