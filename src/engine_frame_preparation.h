#pragma once

#include "engine_frame_submission.h"
#include "modern_queue_lifecycle.h"
#include <array>
#include <algorithm>
#include <span>

#include <cstdint>

namespace w3vr::frame_preparation {

// Examined Remastered 5.00c producer, not a complete compatibility profile.
inline constexpr std::uint32_t producer_rva = 0x02274B80;
inline constexpr std::uint32_t factory_return_rva = 0x022758B5;
inline constexpr std::uint32_t engine_prepare_rva = 0x0224C600;
inline constexpr std::uint32_t engine_prepare_return_rva = 0x022758EE;
inline constexpr std::uint32_t effects_tick_rva = 0x02367890;
inline constexpr std::uint32_t effects_tick_return_rva = 0x02275921;
inline constexpr std::uint32_t effects_apply_rva = 0x02367B60;
inline constexpr std::uint32_t effects_apply_return_rva = 0x02275929;
inline constexpr std::uint32_t command_construct_return_rva = 0x02275941;
inline constexpr std::uint32_t engine_global_rva = 0x05A518F8;
inline constexpr std::uint32_t engine_vtable_rva = 0x037A8F18;
inline constexpr std::uint32_t empty_world_callback_rva = 0x0031D810;

inline constexpr std::array<std::uint8_t,16> producer_500c_prefix{0x40,0x55,0x57,0x48,0x8D,0xAC,0x24,0x98,0x08,0xFF,0xFF,0xB8,0x68,0xF8,0x00,0x00};
inline constexpr std::array<std::uint8_t,18> engine_prepare_500c_prefix{0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x57,0x48,0x8B,0xEC,0x48,0x81,0xEC,0x80,0x00,0x00,0x00};
inline constexpr std::array<std::uint8_t,20> effects_tick_500c_prefix{0x40,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05,0x93,0xA0,0x6E,0x03,0x48,0x8B,0x35,0xF4,0xA5,0x6E,0x03};
inline constexpr std::array<std::uint8_t,16> effects_apply_500c_prefix{0x48,0x85,0xD2,0x0F,0x84,0xBA,0x0B,0x00,0x00,0x48,0x8B,0xC4,0x48,0x89,0x50,0x10};
inline constexpr std::array<std::uint8_t,17> command_construct_500c_prefix{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x89,0x51,0x08,0x48,0x8D,0x05,0x67,0x89,0x6B,0x01};
inline constexpr std::array<std::uint8_t,16> producer_1048522_prefix{0x40,0x55,0x57,0x48,0x8D,0xAC,0x24,0x98,0x08,0xFF,0xFF,0xB8,0x68,0xF8,0x00,0x00};
inline constexpr std::array<std::uint8_t,18> engine_prepare_1048522_prefix{0x48,0x89,0x54,0x24,0x10,0x55,0x53,0x57,0x48,0x8B,0xEC,0x48,0x81,0xEC,0x80,0x00,0x00,0x00};
inline constexpr std::array<std::uint8_t,20> effects_tick_1048522_prefix{0x40,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05,0x2B,0xBC,0x6E,0x03,0x48,0x8B,0x35,0x8C,0xC1,0x6E,0x03};
inline constexpr std::array<std::uint8_t,16> effects_apply_1048522_prefix{0x48,0x85,0xD2,0x0F,0x84,0xBA,0x0B,0x00,0x00,0x48,0x8B,0xC4,0x48,0x89,0x50,0x10};
inline constexpr std::array<std::uint8_t,17> command_construct_1048522_prefix{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x89,0x51,0x08,0x48,0x8D,0x05,0x47,0xAE,0x6B,0x01};
struct HookTarget {std::uintptr_t rva;std::span<const std::uint8_t> prefix;};
struct Profile {
    const modern_queue_lifecycle::Profile* lifecycle;
    const frame_submission::Profile* submission;
    std::array<HookTarget,5> hooks;
    std::uintptr_t factory_return,prepare_return,tick_return,apply_return,construct_return;
    std::uintptr_t engine_global,engine_vtable,effects_state_global,effects_world_global,empty_world,publication_return;
};
inline constexpr Profile remastered_500c{&modern_queue_lifecycle::remastered_500c,
    &frame_submission::remastered_500c,{{{0x02274B80,producer_500c_prefix},{0x0224C600,engine_prepare_500c_prefix},{0x02367890,effects_tick_500c_prefix},{0x02367B60,effects_apply_500c_prefix},{0x02297670,command_construct_500c_prefix}}},
    factory_return_rva,engine_prepare_return_rva,effects_tick_return_rva,effects_apply_return_rva,
    command_construct_return_rva,engine_global_rva,engine_vtable_rva,0x05A51E98,0x05A51930,empty_world_callback_rva,0x0227594F};
inline constexpr Profile remastered_1048522{&modern_queue_lifecycle::remastered_1048522,
    &frame_submission::remastered_1048522,{{{0x0227D1F0,producer_1048522_prefix},{0x02254B70,engine_prepare_1048522_prefix},{0x0236FCF0,effects_tick_1048522_prefix},{0x0236FFC0,effects_apply_1048522_prefix},{0x0229FD80,command_construct_1048522_prefix}}},
    0x0227DF27,0x0227DF60,0x0227DF93,0x0227DF9B,0x0227DFB3,
    0x05A5B8F0,0x037B3C10,0x05A5BE90,0x05A5B928,0x0031C320,0x0227DFC1};
inline bool known(const Profile* profile){return profile==&remastered_500c || profile==&remastered_1048522;}
inline const Profile* selected(const engine_camera::TemporalContract* contract){
    if(contract==&engine_camera::remastered_500c)return &remastered_500c;
    if(contract==&engine_camera::remastered_1048522)return &remastered_1048522;
    return nullptr;
}
inline bool ready_for(const engine_camera::TemporalContract* contract,const Profile* installed,
    const modern_queue_lifecycle::Profile* lifecycle,bool preparation_ready,bool lifecycle_ready){
    return preparation_ready && known(installed) && selected(contract)==installed &&
        installed->lifecycle==lifecycle && frame_submission::selected(contract)==installed->submission &&
        modern_queue_lifecycle::ready_for(contract,lifecycle,lifecycle_ready);
}
enum class Boundary {factory,prepare,tick,apply,construct,publication};
inline bool allows(const Profile* profile,Boundary boundary,std::uintptr_t caller){
    if(!known(profile))return false;
    switch(boundary){
    case Boundary::factory:return caller==profile->factory_return;
    case Boundary::prepare:return caller==profile->prepare_return;
    case Boundary::tick:return caller==profile->tick_return;
    case Boundary::apply:return caller==profile->apply_return;
    case Boundary::construct:return caller==profile->construct_return;
    case Boundary::publication:return caller==profile->publication_return;
    default:return false;
    }
}
inline bool empty_callback(const Profile* profile,std::uintptr_t module,std::uintptr_t callback,
    std::span<const std::uint8_t> code){
    std::uintptr_t expected{};
    return known(profile) && modern_command_queue::add(module,profile->empty_world,expected) &&
        callback==expected && code.size()>=3 && code[0]==0xC2 && code[1]==0 && code[2]==0;
}
inline bool prefix_matches(std::span<const std::uint8_t> expected,std::span<const std::uint8_t> actual){
    return !expected.empty() && actual.size()>=expected.size() &&
        std::equal(expected.begin(),expected.end(),actual.begin());
}
// Prefix checks and one-attempt admission belong to the host. These callbacks
// are real MinHook operations in the host and spies in CPU tests. Retaining
// forwarders after partial failure does not establish an all-call/unload barrier.
inline constexpr std::size_t observer_count=6;
inline std::uintptr_t observer_rva(const Profile* profile,std::size_t index){
    if(!known(profile) || index>=observer_count)return 0;
    return index<profile->hooks.size()?profile->hooks[index].rva:profile->submission->dispatch;
}
template<class Create,class Enable,class Disable,class Current>
bool install_observers(const engine_camera::TemporalContract* contract,const Profile* profile,
    Create create,Enable enable,Disable disable,Current current){
    if(!known(profile) || selected(contract)!=profile)return false;
    std::size_t created{},enabled{};
    for(std::size_t i=0;i<observer_count;++i){if(!current() || !create(i))break;++created;}
    if(created==observer_count)for(std::size_t i=0;i<observer_count;++i){if(!current() || !enable(i))break;++enabled;}
    if(created==observer_count && enabled==observer_count && current())return true;
    for(std::size_t i=0;i<enabled;++i)disable(i);
    return false;
}
// Rejected entries mask the previous producer while Original runs, then restore
// it during normal return or C++ unwinding. The scope owns no Native object.
template<class Producer> class ScopedProducer {
    Producer*& slot_;Producer* previous_;bool admitted_;
public:
    ScopedProducer(Producer*& slot,Producer* entry,bool admitted)
        :slot_(slot),previous_(slot),admitted_(admitted && entry!=nullptr){slot_=admitted_?entry:nullptr;}
    ~ScopedProducer(){slot_=previous_;}
    ScopedProducer(const ScopedProducer&)=delete;
    ScopedProducer& operator=(const ScopedProducer&)=delete;
    bool admitted()const{return admitted_;}
};

#if defined(_MSC_VER)
using PrepareFrame = void(__fastcall*)(void*, void*);
#else
using PrepareFrame = void(*)(void*, void*);
#endif
using InvalidatePair = void(*)(void*, void*, void*);

// Lives on the native producer's stack. The primary frame stays owned by the
// game; only the duplicate's factory reference belongs to this object. No
// stack-backed descriptor or pointed descriptor resource is retained here.
// Natural preparation must finish before either duplicate preparation or
// command dispatch. Shared effects time is observed once, never replayed.
class PendingPair {
public:
    PendingPair() = default;
    PendingPair(const PendingPair&) = delete;
    PendingPair& operator=(const PendingPair&) = delete;
    ~PendingPair() { cancel(); }

    bool arm(const Profile* profile,void* primary, void* duplicate, frame_submission::ReleaseFrame release,
        InvalidatePair invalidate, void* owner) {
        if (!known(profile) || primary == nullptr || duplicate == nullptr || primary == duplicate ||
            release == nullptr || invalidate == nullptr || armed_) return false;
        profile_ = profile;
        primary_ = primary;
        duplicate_ = duplicate;
        release_ = release;
        invalidate_ = invalidate;
        owner_ = owner;
        armed_ = true;
        return true;
    }

    bool accepts(void* frame) const { return armed_ && duplicate_ && frame == primary_; }
    bool pending()const{return duplicate_!=nullptr;}
    bool available() const { return !armed_; }

    void observe_engine(const Profile* profile,void* frame, void* receiver, PrepareFrame prepare,
        bool shared_callback_absent) {
        if (!accepts(frame)) return;
        if (profile!=profile_ || !known(profile) || engine_seen_ || tick_seen_ || effects_seen_ || receiver == nullptr ||
            prepare == nullptr || !shared_callback_absent) { poisoned_ = true; return; }
        engine_ = receiver;
        engine_prepare_ = prepare;
        engine_seen_ = true;
    }

    void observe_effects_tick(const Profile* profile) {
        if (!duplicate_) return;
        if (profile!=profile_ || !known(profile) || !engine_seen_ || tick_seen_ || effects_seen_) { poisoned_ = true; return; }
        tick_seen_ = true;
    }

    void observe_effects(const Profile* profile,void* frame, void* first_argument, PrepareFrame apply) {
        if (!accepts(frame)) return;
        if (profile!=profile_ || !known(profile) || !engine_seen_ || !tick_seen_ || effects_seen_ || apply == nullptr) {
            poisoned_ = true; return;
        }
        effects_first_argument_ = first_argument;
        effects_apply_ = apply;
        effects_seen_ = true;
    }

    enum class Completion { ignored, rejected, dispatched, submission_failed };

    Completion at_native_command(void* frame, const frame_submission::NativeCommandRoute& route,
        bool context_still_matches) {
        if (!accepts(frame)) return Completion::ignored;
        if (poisoned_ || !known(profile_) || route.profile!=profile_->submission ||
            !engine_seen_ || tick_seen_ != effects_seen_ ||
            !context_still_matches || !route.renderer_ready || route.allocate == nullptr ||
            route.construct == nullptr || route.dispatch == nullptr ||
            route.unavailable_command == nullptr) {
            cancel();
            return Completion::rejected;
        }
        // Clear ownership before native calls: reentrant callbacks cannot
        // replay or release this same duplicate a second time.
        void* duplicate = duplicate_;
        duplicate_ = nullptr;
        engine_prepare_(engine_, duplicate);
        if (effects_seen_) effects_apply_(effects_first_argument_, duplicate);
        const auto result = frame_submission::submit_owned_frame(route, duplicate, release_);
        if (result != frame_submission::Result::dispatched) {
            invalidate_(owner_, primary_, duplicate);
            return Completion::submission_failed;
        }
        return Completion::dispatched;
    }

    void cancel() {
        if (duplicate_ == nullptr) return;
        void* duplicate = duplicate_;
        duplicate_ = nullptr;
        invalidate_(owner_, primary_, duplicate);
        release_(duplicate);
    }

private:
    const Profile* profile_{};
    void* primary_{};
    void* duplicate_{};
    frame_submission::ReleaseFrame release_{};
    InvalidatePair invalidate_{};
    void* owner_{};
    bool armed_{};
    bool poisoned_{};
    bool engine_seen_{};
    bool tick_seen_{};
    bool effects_seen_{};
    void* engine_{};
    PrepareFrame engine_prepare_{};
    void* effects_first_argument_{};
    PrepareFrame effects_apply_{};
};

} // namespace w3vr::frame_preparation
