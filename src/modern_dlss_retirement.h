#pragma once

#include "modern_dlss_recording.h"
#include <memory>
#include <span>

namespace w3vr::modern_dlss_retirement {

struct RecordingData;
struct QueueState;
class Timeline;

// Shared immutable recording ownership. Reset abandons future replay, but
// cannot release references already held by another queue's submission.
class Recording {
public:
    explicit operator bool() const noexcept { return bool(data_); }
    const modern_dlss_recording::ClosedRecording* get() const noexcept;
private:
    std::shared_ptr<RecordingData> data_;
    friend Recording retain(modern_dlss_recording::ClosedRecording&&);
    friend class Timeline;
};
Recording retain(modern_dlss_recording::ClosedRecording&&);

// The private fence is also an owned identity anchor. A ticket from a deleted
// timeline cannot match a new timeline through pointer/serial reuse.
class Ticket {
public:
    explicit operator bool() const noexcept { return anchor_ && serial_ != 0; }
private:
    Microsoft::WRL::ComPtr<IUnknown> anchor_;
    std::uint64_t serial_{};
    friend class Timeline;
};

enum class Status { Unknown, Prepared, Executing, Pending, Complete, Quarantined, DeviceLost };
using Signal = HRESULT (*)(ID3D12CommandQueue*, ID3D12Fence*, std::uint64_t);

// Host must serialize actual Reset/Close/producer/Execute/Signal observations
// with queue forwarding. This module does NOT call Execute, change its array,
// invoke Streamline, or claim a stereo image. Internal locks protect lifetime
// bookkeeping, not the correctness of a future game's hook ordering.
class Timeline {
public:
    explicit Timeline(modern_dlss_ownership::OwnedQueue, Signal = nullptr);
    ~Timeline();
    Timeline(const Timeline&) = delete;
    Timeline& operator=(const Timeline&) = delete;
    Timeline(Timeline&&) = delete;
    Timeline& operator=(Timeline&&) = delete;
    explicit operator bool() const noexcept;

    // Canonical IDs must come from owned native interfaces of the ORIGINAL
    // submitted array. Same device alone never authorizes a different queue.
    Ticket prepare(const Recording&, modern_dlss_recording::Stamp actual,
        const modern_dlss_ownership::OwnedQueue& actual_queue,
        std::span<IUnknown* const> submitted_ids);
    Recording find(modern_dlss_recording::Stamp) const;
    bool cancel(const Ticket&); // Only before Execute might have been called.
    bool before_execute(const Ticket&); // Retain BEFORE forwarding exactly once.
    HRESULT after_execute(const Ticket&); // Only after that forwarding returns.
    HRESULT retry_signal(const Ticket&); // Only a returned, failed Signal.
    Status status(const Ticket&) const;
    Recording completed_recording(const Ticket&) const;
    HRESULT completion_event(const Ticket&, HANDLE) const;
    bool release_completed(const Ticket&);
    std::size_t pending() const;
    static constexpr bool stereo_image_verified = false;
private:
    QueueState* state_{};
};

// Call only AFTER a proven successful native Reset, with the NEW epoch.
// Retains in-flight references, abandons only older recordings of that exact
// canonical command, and applies even to orphaned timelines after shutdown.
void observe_reset(modern_dlss_recording::Stamp new_recording) noexcept;
// Destruction cannot release submitted objects without a valid private fence.
// Failed signals/device removal remain in quarantine; no destructor allocation.
// Unresolved orphan states intentionally remain owned until proof/process exit.
std::size_t collect_orphans() noexcept;
std::size_t orphan_count() noexcept;

} // namespace w3vr::modern_dlss_retirement
