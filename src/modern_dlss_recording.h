#pragma once

#include "modern_dlss_ownership.h"
#include <memory>
#include <mutex>
#include <vector>

namespace w3vr::modern_dlss_recording {

using modern_dlss_ownership::OwnedEvaluation;
using Microsoft::WRL::ComPtr;

namespace detail {
enum class Phase { Unknown, Open, Closed, Broken };
struct Clock {
    ComPtr<IUnknown> command, device;
    std::mutex mutex;
    std::uint64_t epoch{}, observer{};
    Phase phase{Phase::Unknown};
};
struct ClockNode { std::weak_ptr<Clock> clock; ClockNode* next{}; };
// POD roots have no shutdown destructor releasing COM under the loader lock.
// Weak entries do not keep a native object alive; stamps and ledgers do.
inline SRWLOCK clocks_lock = SRWLOCK_INIT;
inline ClockNode* clocks{};
inline std::shared_ptr<Clock> acquire_clock(const modern_dlss_ownership::OwnedCommand& proof) {
    struct Lock {
        Lock() { AcquireSRWLockExclusive(&clocks_lock); }
        ~Lock() { ReleaseSRWLockExclusive(&clocks_lock); }
    } guard;
    auto** link = &clocks;
    while (*link) {
        auto* node = *link;
        if (auto clock = node->clock.lock()) {
            if (clock->command.Get() == proof.command_identity.Get())
                return clock->device.Get() == proof.device_identity.Get() ? clock : nullptr;
            link = &node->next;
        } else { *link = node->next; delete node; }
    }
    auto node = std::make_unique<ClockNode>();
    auto clock = std::make_shared<Clock>();
    clock->command = proof.command_identity; clock->device = proof.device_identity;
    node->clock = clock; node->next = clocks; clocks = node.release();
    return clock;
}
} // namespace detail

struct Stamp {
    const IUnknown* command{};
    std::uint64_t epoch{};
    bool operator==(const Stamp&) const = default;
    explicit operator bool() const { return clock_ && command == clock_->command.Get() && epoch != 0; }
    bool current(bool require_closed = false) const {
        if (!*this) return false;
        std::scoped_lock guard(clock_->mutex);
        return epoch == clock_->epoch && (require_closed ? clock_->phase == detail::Phase::Closed :
            clock_->phase == detail::Phase::Open || clock_->phase == detail::Phase::Closed);
    }
private:
    std::shared_ptr<detail::Clock> clock_;
    Stamp(const std::shared_ptr<detail::Clock>& clock, std::uint64_t value)
        : command(clock->command.Get()), epoch(value), clock_(clock) {}
    friend class Ledger;
public:
    Stamp() = default;
};

// A closed CPU recording, NOT a submitted/completed image. The queue adapter
// must take ownership BEFORE Execute and retain it through its exact fence.
// Repeated submission and failed Signal require a separate retirement policy;
// this object must never be dropped after Execute just because Reset succeeds.
struct ClosedRecording {
    Stamp stamp{};
    std::vector<OwnedEvaluation> evaluations;
    ClosedRecording() = default;
    ClosedRecording(ClosedRecording&&) = default;
    ClosedRecording& operator=(ClosedRecording&&) = default;
    ClosedRecording(const ClosedRecording&) = delete;
    ClosedRecording& operator=(const ClosedRecording&) = delete;
    explicit operator bool() const { return bool(stamp) && !evaluations.empty(); }
    static constexpr bool gpu_completion_verified = false;
};

// Not thread safe: the future host adapter must serialize the real native
// Reset/Close/producer/Execute observations. No game hook provides that proof
// yet. Construct only from acquire(), never from a borrowed legacy pointer.
// This ledger does not call Reset, Close, Execute, Signal or the SDK itself.
class Ledger {
public:
    explicit Ledger(const modern_dlss_ownership::OwnedCommand& proof) {
        if (proof && proof.command_identity && proof.device_identity) {
            command_ = proof.command_identity;
            device_ = proof.device_identity;
            clock_ = detail::acquire_clock(proof);
            if (!clock_) return;
            std::scoped_lock guard(clock_->mutex);
            // Rebinding cannot inherit an unobserved open/closed recording.
            // It invalidates older observers, but conserves the shared counter.
            if (clock_->observer == UINT64_MAX || clock_->phase == detail::Phase::Broken) {
                broken_ = true; return;
            }
            observer_ = ++clock_->observer;
            clock_->phase = detail::Phase::Unknown;
        }
    }
    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;
    Ledger(Ledger&&) = delete;
    Ledger& operator=(Ledger&&) = delete;

    // A successful observed Reset discards ONLY unsubmitted observations.
    // A batch already taken is independent and keeps all its COM references.
    // Failed Reset does not advance the epoch; its recording is not accepted
    // again until a successful Reset. A failed Close is permanently invalid.
    bool after_reset(HRESULT result) {
        if (!clock_ || broken_) return false;
        std::scoped_lock guard(clock_->mutex);
        if (observer_ != clock_->observer || clock_->phase == detail::Phase::Broken) return false;
        if (FAILED(result)) { uncertain_ = true; clock_->phase = detail::Phase::Unknown; return false; }
        if (clock_->epoch == UINT64_MAX) { broken_ = true; clock_->phase = detail::Phase::Broken; return false; }
        epoch_ = ++clock_->epoch;
        pending_.clear();
        phase_ = Phase::Open;
        uncertain_ = false;
        clock_->phase = detail::Phase::Open;
        return true;
    }
    bool after_close(HRESULT result) {
        if (!clock_ || broken_) return false;
        std::scoped_lock guard(clock_->mutex);
        if (observer_ != clock_->observer || epoch_ != clock_->epoch) return false;
        if (FAILED(result) || phase_ != Phase::Open || uncertain_) {
            broken_ = true;
            clock_->phase = FAILED(result) ? detail::Phase::Broken : detail::Phase::Unknown;
            return false;
        }
        phase_ = Phase::Closed;
        clock_->phase = detail::Phase::Closed;
        return true;
    }
    // Capture before the producer/SDK call and check again when admitting its
    // output. Capturing only afterwards cannot detect Reset during that call.
    Stamp open_stamp() const {
        if (!clock_) return {};
        std::scoped_lock guard(clock_->mutex);
        return observer_ == clock_->observer && epoch_ == clock_->epoch &&
            clock_->phase == detail::Phase::Open && phase_ == Phase::Open && !uncertain_ && !broken_
            ? Stamp{clock_, epoch_} : Stamp{};
    }
    bool record(OwnedEvaluation&& evaluation, Stamp before) {
        if (!clock_) return false;
        std::scoped_lock guard(clock_->mutex);
        if (!before || before != Stamp{clock_, epoch_} || observer_ != clock_->observer ||
            epoch_ != clock_->epoch || clock_->phase != detail::Phase::Open ||
            phase_ != Phase::Open || uncertain_ || broken_ || !evaluation ||
            !evaluation.identity.valid() ||
            evaluation.viewport != engine_dlss::constructed_viewport ||
            evaluation.command_identity.Get() != command_.Get() ||
            evaluation.device_identity.Get() != device_.Get() ||
            pending_.size() == 64) return false;
        for (const auto& item : pending_)
            if (item.identity.pair_id == evaluation.identity.pair_id &&
                item.identity.generation == evaluation.identity.generation &&
                item.identity.eye == evaluation.identity.eye) return false;
        pending_.push_back(std::move(evaluation));
        return true;
    }
    ClosedRecording take_closed(Stamp expected) {
        ClosedRecording result;
        if (!clock_) return result;
        std::scoped_lock guard(clock_->mutex);
        if (!expected || expected != Stamp{clock_, epoch_} || observer_ != clock_->observer ||
            epoch_ != clock_->epoch || clock_->phase != detail::Phase::Closed ||
            phase_ != Phase::Closed || uncertain_ || broken_ || pending_.empty())
            return result;
        result.stamp = expected;
        result.evaluations = std::move(pending_);
        phase_ = Phase::Taken;
        return result;
    }
    std::uint64_t epoch() const { return epoch_; }
    std::size_t pending() const { return pending_.size(); }

private:
    enum class Phase { Unknown, Open, Closed, Taken };
    ComPtr<IUnknown> command_, device_; // Holding identity prevents address reuse.
    std::shared_ptr<detail::Clock> clock_;
    std::uint64_t epoch_{}, observer_{};
    Phase phase_{Phase::Unknown};
    bool uncertain_{}, broken_{};
    std::vector<OwnedEvaluation> pending_;
};
} // namespace w3vr::modern_dlss_recording
