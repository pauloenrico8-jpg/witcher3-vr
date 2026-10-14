#pragma once
#include "modern_graphics_binding.h"
namespace w3vr::modern_xr_graphics_binding {
using Graphics = modern_graphics_binding::Snapshot;
struct Ticket {Graphics graphics;std::uintptr_t instance{};std::uint64_t nonce{};};
struct Data {Graphics graphics;std::uintptr_t instance{},session{};std::uint64_t nonce{};};
using Snapshot = std::shared_ptr<const Data>;
inline bool valid(const Graphics& g) {
    return g && g->device && g->queue && g->chain && g->device_identity &&
        g->queue_identity && g->chain_identity && modern_graphics_binding::finite(g->epoch) &&
        g->present_count && g->present_count==g->buffer_count;
}
inline bool same_endpoints(const Graphics& a,const Graphics& b) {
    return valid(a) && valid(b) && a->device.Get()==b->device.Get() &&
        a->queue.Get()==b->queue.Get() && a->device_identity.Get()==b->device_identity.Get() &&
        a->queue_identity.Get()==b->queue_identity.Get() && a->chain_identity.Get()==b->chain_identity.Get();
}
// This root retains interfaces. It does not freeze pixels, prove queue ordering,
// serialize XR calls, or provide a loader/all-calls barrier. Host must verify
// concrete Native endpoints and current graphics outside this mutex each time.
class Registry {
    mutable std::mutex mutex_;
    std::atomic<bool> accepting_{true};
    bool attempted_{},finished_{};
    Ticket staged_{};
    Snapshot active_;
public:
    bool begin(const Graphics& graphics,std::uintptr_t instance,bool native_verified,Ticket& out) {
        if(!native_verified || !instance || !valid(graphics))return false;
        std::lock_guard lock{mutex_};
        if(!accepting_.load(std::memory_order_acquire) || attempted_)return false;
        attempted_=true;staged_={graphics,instance,1};out=staged_;return true;
    }
    bool complete(const Ticket& ticket,bool original_success,std::uintptr_t session,
        const Graphics& current,bool native_verified) {
        {
            std::lock_guard lock{mutex_};
            if(!accepting_.load(std::memory_order_acquire) || finished_ || !attempted_ ||
                !ticket.nonce || ticket.nonce!=staged_.nonce || ticket.instance!=staged_.instance ||
                ticket.graphics.get()!=staged_.graphics.get())return false;
            finished_=true; // Exact callback failure cannot rearm this attempt.
            if(!native_verified || !original_success || !session || !valid(current) ||
                current.get()!=staged_.graphics.get())return false;
        }
        auto next=std::make_shared<Data>(Data{ticket.graphics,ticket.instance,session,ticket.nonce});
        std::lock_guard lock{mutex_};
        if(!accepting_.load(std::memory_order_acquire) || active_)return false;
        active_=std::move(next);return true;
    }
    Snapshot acquire(std::uintptr_t instance,std::uintptr_t session,const Graphics& current,
        bool native_verified)const {
        Snapshot result;
        {std::lock_guard lock{mutex_};
            if(accepting_.load(std::memory_order_acquire) && native_verified && active_ &&
                active_->instance==instance && active_->session==session &&
                same_endpoints(active_->graphics,current))result=active_;
        }
        if(!accepting_.load(std::memory_order_acquire))result.reset();return result;
    }
    bool registered(std::uintptr_t instance,std::uintptr_t session)const {
        std::lock_guard lock{mutex_};
        return active_ && active_->instance==instance && active_->session==session;
    }
    bool current(const Snapshot& snapshot,std::uintptr_t instance,std::uintptr_t session,
        const Graphics& graphics,bool native_verified)const {
        return snapshot && acquire(instance,session,graphics,native_verified).get()==snapshot.get();
    }
    // No reset/retarget/retry and no release: a real drain/destruction protocol
    // is required before replacing the runtime's immutable queue association.
    void stop_no_release(){accepting_.store(false,std::memory_order_release);}
};
} // namespace w3vr::modern_xr_graphics_binding
