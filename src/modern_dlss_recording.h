#pragma once

#include "modern_dlss_ownership.h"
#include <vector>

namespace w3vr::modern_dlss_recording {

using modern_dlss_ownership::OwnedEvaluation;
using Microsoft::WRL::ComPtr;

struct Stamp {
    const IUnknown* command{};
    std::uint64_t epoch{};
    bool operator==(const Stamp&) const = default;
    explicit operator bool() const { return command && epoch != 0; }
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
    explicit Ledger(const OwnedEvaluation& proof) {
        if (proof && proof.command_identity && proof.device_identity) {
            command_ = proof.command_identity;
            device_ = proof.device_identity;
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
        if (!command_ || broken_) return false;
        if (FAILED(result)) { uncertain_ = true; return false; }
        if (epoch_ == UINT64_MAX) { broken_ = true; return false; }
        ++epoch_;
        pending_.clear();
        phase_ = Phase::Open;
        uncertain_ = false;
        return true;
    }
    bool after_close(HRESULT result) {
        if (!command_ || broken_) return false;
        if (FAILED(result) || phase_ != Phase::Open || uncertain_) {
            broken_ = true;
            return false;
        }
        phase_ = Phase::Closed;
        return true;
    }
    // Capture before the producer/SDK call and check again when admitting its
    // output. Capturing only afterwards cannot detect Reset during that call.
    Stamp open_stamp() const {
        return phase_ == Phase::Open && !uncertain_ && !broken_
            ? Stamp{command_.Get(), epoch_} : Stamp{};
    }
    bool record(OwnedEvaluation&& evaluation, Stamp before) {
        if (!before || before != open_stamp() || !evaluation ||
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
        if (!expected || expected != Stamp{command_.Get(), epoch_} ||
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
    std::uint64_t epoch_{};
    Phase phase_{Phase::Unknown};
    bool uncertain_{}, broken_{};
    std::vector<OwnedEvaluation> pending_;
};
} // namespace w3vr::modern_dlss_recording
