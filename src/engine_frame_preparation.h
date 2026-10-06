#pragma once

#include "engine_frame_submission.h"

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

    bool arm(void* primary, void* duplicate, frame_submission::ReleaseFrame release,
        InvalidatePair invalidate, void* owner) {
        if (primary == nullptr || duplicate == nullptr || primary == duplicate ||
            release == nullptr || invalidate == nullptr || armed_) return false;
        primary_ = primary;
        duplicate_ = duplicate;
        release_ = release;
        invalidate_ = invalidate;
        owner_ = owner;
        armed_ = true;
        return true;
    }

    bool accepts(void* frame) const { return armed_ && duplicate_ && frame == primary_; }
    bool available() const { return !armed_; }

    void observe_engine(void* frame, void* receiver, PrepareFrame prepare,
        bool shared_callback_absent) {
        if (!accepts(frame)) return;
        if (engine_seen_ || tick_seen_ || effects_seen_ || receiver == nullptr ||
            prepare == nullptr || !shared_callback_absent) { poisoned_ = true; return; }
        engine_ = receiver;
        engine_prepare_ = prepare;
        engine_seen_ = true;
    }

    void observe_effects_tick() {
        if (!duplicate_) return;
        if (!engine_seen_ || tick_seen_ || effects_seen_) { poisoned_ = true; return; }
        tick_seen_ = true;
    }

    void observe_effects(void* frame, void* first_argument, PrepareFrame apply) {
        if (!accepts(frame)) return;
        if (!engine_seen_ || !tick_seen_ || effects_seen_ || apply == nullptr) {
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
        if (poisoned_ || !engine_seen_ || tick_seen_ != effects_seen_ ||
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
