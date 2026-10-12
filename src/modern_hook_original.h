#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace w3vr::modern_hook_original {
// A saved original belongs to one concrete method target. This association
// is not an enabled-hook receipt, an object lease or an unload barrier.
// The host serializes bind calls with its existing hook-mutation mutex.
class Route {
    std::atomic<void*> target_{}, original_{};
    std::atomic<bool> bound_{}, conflict_{};
public:
    Route()=default;
    Route(const Route&)=delete;
    Route& operator=(const Route&)=delete;
    bool bind(void* target,void* original) noexcept {
        if(!target || !original || target==original || conflict_.load(std::memory_order_acquire))return false;
        if(bound_.load(std::memory_order_acquire)){
            if(target_.load(std::memory_order_relaxed)==target && original_.load(std::memory_order_relaxed)==original)return true;
            conflict_.store(true,std::memory_order_release);return false;
        }
        target_.store(target,std::memory_order_relaxed);
        original_.store(original,std::memory_order_relaxed);
        bound_.store(true,std::memory_order_release);return true;
    }
    void* for_target(void* actual)const noexcept {
        if(!actual || !bound_.load(std::memory_order_acquire) || conflict_.load(std::memory_order_acquire) ||
            target_.load(std::memory_order_relaxed)!=actual)return nullptr;
        return original_.load(std::memory_order_relaxed);
    }
};
using Routes=std::array<Route,4>;
inline void* select(void* actual,const Route& legacy,const Route& native) noexcept {
    auto* old=legacy.for_target(actual);auto* modern=native.for_target(actual);
    // Sharing an exact owner is allowed. Different trampolines for the same
    // target are ambiguous; never choose one by priority or by module name.
    return old && modern && old!=modern ? nullptr : modern?modern:old;
}
// A proxy hook still forwards normally. In modern mode its deeper native
// callback owns observation; the wrapper's identity never gains that epoch.
inline bool wrapper_passthrough(bool modern_requested,bool native_receiver) noexcept {
    return modern_requested && !native_receiver;
}
} // namespace w3vr::modern_hook_original
