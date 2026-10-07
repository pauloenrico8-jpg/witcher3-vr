#include "modern_dlss_retirement.h"
#include <algorithm>
#include <atomic>
#include <vector>

namespace w3vr::modern_dlss_retirement {
namespace m = modern_dlss_ownership;
namespace r = modern_dlss_recording;
using Microsoft::WRL::ComPtr;

struct RecordingData {
    r::ClosedRecording recording;
    std::atomic<bool> abandoned{}, possibly_submitted{};
    explicit RecordingData(r::ClosedRecording&& value) : recording(std::move(value)) {}
};
struct Entry {
    std::shared_ptr<RecordingData> recording;
    std::uint64_t serial{}, value{};
    Status phase{Status::Prepared};
};
struct QueueState {
    SRWLOCK lock = SRWLOCK_INIT;
    m::OwnedQueue queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<IUnknown> anchor;
    Signal signal{};
    std::uint64_t serial{}, value{};
    bool blocked{};
    std::vector<Entry> entries;
    std::vector<std::shared_ptr<RecordingData>> recordings;
    QueueState* next{};
};
namespace {
// POD lifetime roots: no static vector destructor or allocation at shutdown.
SRWLOCK registry_lock = SRWLOCK_INIT;
QueueState* active{};
QueueState* orphans{};
struct RecordNode { std::weak_ptr<RecordingData> recording; RecordNode* next{}; };
RecordNode* records{};
struct Lock {
    SRWLOCK* lock;
    explicit Lock(SRWLOCK& value) : lock(&value) { AcquireSRWLockExclusive(lock); }
    ~Lock() { ReleaseSRWLockExclusive(lock); }
};
void collect_records() {
    auto** link = &records;
    while (*link) {
        auto* node = *link;
        if (node->recording.expired()) { *link = node->next; delete node; }
        else link = &node->next;
    }
}
HRESULT native_signal(ID3D12CommandQueue* queue, ID3D12Fence* fence, std::uint64_t value) {
    return queue->Signal(fence, value);
}
Entry* lookup(QueueState& state, std::uint64_t serial) {
    const auto found = std::find_if(state.entries.begin(), state.entries.end(),
        [serial](const Entry& item) { return item.serial == serial; });
    return found == state.entries.end() ? nullptr : &*found;
}
Status phase(const QueueState& state, const Entry& entry) {
    if (entry.phase != Status::Pending) return entry.phase;
    const auto completed = state.fence->GetCompletedValue();
    if (completed == UINT64_MAX) return Status::DeviceLost;
    return m::fence_reached(completed, entry.value) ? Status::Complete : Status::Pending;
}
void prune(QueueState& state) {
    std::erase_if(state.recordings, [&state](const auto& recording) {
        const bool pending = std::any_of(state.entries.begin(), state.entries.end(),
            [&recording](const Entry& entry) { return entry.recording == recording; });
        return !pending && (recording->abandoned.load() || !recording->possibly_submitted.load());
    });
}
void unlink(QueueState*& head, QueueState* state) {
    auto** link = &head;
    while (*link && *link != state) link = &(*link)->next;
    if (*link) *link = state->next;
}
void reset(QueueState& state, r::Stamp stamp) {
    for (auto& recording : state.recordings) {
        const auto old = recording->recording.stamp;
        if (old.command == stamp.command && old.epoch < stamp.epoch)
            recording->abandoned.store(true);
    }
    prune(state);
}
HRESULT signal(QueueState& state, Entry& entry) {
    if (state.value >= UINT64_MAX - 1) {
        state.blocked = true; entry.phase = Status::Quarantined;
        return E_FAIL;
    }
    // Reserve a different finite number even on failure: never reuse a value
    // whose attempted submission has uncertain driver/runtime outcome.
    entry.value = ++state.value;
    const auto result = state.signal(state.queue.queue.Get(), state.fence.Get(), entry.value);
    entry.phase = SUCCEEDED(result) ? Status::Pending : Status::Quarantined;
    state.blocked = std::any_of(state.entries.begin(), state.entries.end(),
        [](const Entry& item) { return item.phase == Status::Quarantined; });
    return result;
}
} // namespace

const r::ClosedRecording* Recording::get() const noexcept {
    return data_ ? &data_->recording : nullptr;
}
Recording retain(r::ClosedRecording&& value) {
    Recording result;
    if (!value || value.evaluations.size() > 64) return result;
    const auto& first = value.evaluations.front();
    for (const auto& item : value.evaluations)
        if (!item || !item.identity.valid() || !item.device ||
            item.viewport != engine_dlss::constructed_viewport ||
            item.command_identity.Get() != value.stamp.command ||
            !item.device_identity || item.device_identity.Get() != first.device_identity.Get())
            return result;
    for (const auto& item : value.evaluations)
        for (std::size_t index = 0; index < item.resources.size(); ++index)
            if (!item.resources[index] || !item.resource_identities[index]) return result;
    auto node = std::make_unique<RecordNode>(); // Allocate before moving caller ownership.
    result.data_ = std::make_shared<RecordingData>(std::move(value));
    node->recording = result.data_;
    Lock registry(registry_lock);
    collect_records();
    node->next = records; records = node.release();
    return result;
}
Timeline::Timeline(m::OwnedQueue queue, Signal callback) {
    if (!queue || !queue.queue_identity || !queue.device || !queue.device_identity) return;
    auto state = std::make_unique<QueueState>();
    state->queue = std::move(queue);
    if (FAILED(state->queue.device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&state->fence))) || !state->fence ||
        FAILED(state->fence.As(&state->anchor)) || !state->anchor) return;
    state->signal = callback ? callback : native_signal;
    state->entries.reserve(64);
    state->recordings.reserve(64);
    Lock guard(registry_lock);
    state_ = state.release(); state_->next = active; active = state_;
}
Timeline::operator bool() const noexcept { return state_ != nullptr; }
Timeline::~Timeline() {
    if (!state_) return;
    // Registry -> state is the sole lock order used throughout this module.
    Lock registry(registry_lock);
    bool retained{};
    {
        Lock guard(state_->lock);
        std::erase_if(state_->entries, [this](const Entry& item) {
            return item.phase == Status::Prepared || phase(*state_, item) == Status::Complete;
        });
        prune(*state_);
        retained = !state_->entries.empty() || !state_->recordings.empty();
    }
    unlink(active, state_);
    if (retained) { state_->next = orphans; orphans = state_; }
    else delete state_;
}
Ticket Timeline::prepare(const Recording& recording, r::Stamp actual,
    const m::OwnedQueue& queue, std::span<IUnknown* const> ids) {
    Ticket ticket;
    if (!state_ || !recording || !actual || !queue ||
        queue.queue_identity.Get() != state_->queue.queue_identity.Get() ||
        queue.device_identity.Get() != state_->queue.device_identity.Get() ||
        actual != recording.get()->stamp ||
        std::find(ids.begin(), ids.end(), actual.command) == ids.end()) return ticket;
    for (const auto& item : recording.get()->evaluations)
        if (!m::compatible_queue_device(state_->queue, item)) return ticket;
    Lock guard(state_->lock);
    if (state_->blocked || state_->entries.size() == 64 ||
        state_->serial == UINT64_MAX || recording.data_->abandoned.load()) return ticket;
    const auto found = std::find_if(state_->recordings.begin(), state_->recordings.end(),
        [actual](const auto& item) { return item->recording.stamp == actual; });
    if (found != state_->recordings.end() && *found != recording.data_) return ticket;
    if (found == state_->recordings.end()) {
        if (state_->recordings.size() == 64) return ticket;
        state_->recordings.push_back(recording.data_);
    }
    state_->entries.push_back({recording.data_, ++state_->serial});
    ticket.anchor_ = state_->anchor; ticket.serial_ = state_->serial;
    return ticket;
}
Recording Timeline::find(r::Stamp stamp) const {
    Recording result;
    if (!state_ || !stamp) return result;
    Lock guard(state_->lock);
    for (const auto& recording : state_->recordings)
        if (!recording->abandoned.load() && recording->recording.stamp == stamp) {
            result.data_ = recording; break;
        }
    return result;
}
bool Timeline::cancel(const Ticket& ticket) {
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return false;
    Lock guard(state_->lock);
    auto* entry = lookup(*state_, ticket.serial_);
    if (!entry || entry->phase != Status::Prepared) return false;
    std::erase_if(state_->entries, [&ticket](const Entry& item) { return item.serial == ticket.serial_; });
    prune(*state_); return true;
}
bool Timeline::before_execute(const Ticket& ticket) {
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return false;
    Lock guard(state_->lock);
    auto* entry = lookup(*state_, ticket.serial_);
    if (!entry || entry->phase != Status::Prepared || state_->blocked ||
        entry->recording->abandoned.load()) return false;
    entry->phase = Status::Executing;
    entry->recording->possibly_submitted.store(true);
    return true;
}
HRESULT Timeline::after_execute(const Ticket& ticket) {
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return E_INVALIDARG;
    Lock guard(state_->lock);
    auto* entry = lookup(*state_, ticket.serial_);
    if (!entry || entry->phase != Status::Executing) return E_INVALIDARG;
    return signal(*state_, *entry);
}
HRESULT Timeline::retry_signal(const Ticket& ticket) {
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return E_INVALIDARG;
    Lock guard(state_->lock);
    auto* entry = lookup(*state_, ticket.serial_);
    if (!entry || entry->phase != Status::Quarantined) return E_INVALIDARG;
    return signal(*state_, *entry);
}
Status Timeline::status(const Ticket& ticket) const {
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return Status::Unknown;
    Lock guard(state_->lock);
    const auto* entry = lookup(*state_, ticket.serial_);
    return entry ? phase(*state_, *entry) : Status::Unknown;
}
HRESULT Timeline::completion_event(const Ticket& ticket, HANDLE event) const {
    if (!state_ || !event || ticket.anchor_.Get() != state_->anchor.Get()) return E_INVALIDARG;
    Lock guard(state_->lock);
    const auto* entry = lookup(*state_, ticket.serial_);
    if (!entry || entry->phase != Status::Pending ||
        state_->fence->GetCompletedValue() == UINT64_MAX) return E_INVALIDARG;
    return state_->fence->SetEventOnCompletion(entry->value, event);
}
Recording Timeline::completed_recording(const Ticket& ticket) const {
    Recording result;
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return result;
    Lock guard(state_->lock);
    const auto* entry = lookup(*state_, ticket.serial_);
    if (entry && phase(*state_, *entry) == Status::Complete) result.data_ = entry->recording;
    return result;
}
bool Timeline::release_completed(const Ticket& ticket) {
    if (!state_ || ticket.anchor_.Get() != state_->anchor.Get()) return false;
    Lock guard(state_->lock);
    auto* entry = lookup(*state_, ticket.serial_);
    if (!entry || phase(*state_, *entry) != Status::Complete) return false;
    std::erase_if(state_->entries, [&ticket](const Entry& item) { return item.serial == ticket.serial_; });
    // Keep replayable command ownership even after this submission retires.
    prune(*state_); return true;
}
std::size_t Timeline::pending() const {
    if (!state_) return 0;
    Lock guard(state_->lock); return state_->entries.size();
}
void observe_reset(r::Stamp stamp) noexcept {
    if (!stamp) return;
    Lock registry(registry_lock);
    // Also abandon handles retained outside any timeline. A later first
    // submission must not resurrect a recording that already crossed Reset.
    collect_records();
    for (auto* node = records; node; node = node->next)
        if (auto recording = node->recording.lock()) {
            const auto old = recording->recording.stamp;
            if (old.command == stamp.command && old.epoch < stamp.epoch)
                recording->abandoned.store(true);
        }
    for (auto* head : {active, orphans})
        for (auto* state = head; state; state = state->next) {
            Lock guard(state->lock); reset(*state, stamp);
        }
}
std::size_t collect_orphans() noexcept {
    Lock registry(registry_lock);
    std::size_t released{};
    auto** link = &orphans;
    while (*link) {
        auto* state = *link;
        bool done{};
        {
            Lock guard(state->lock);
            std::erase_if(state->entries, [state](const Entry& item) {
                return phase(*state, item) == Status::Complete;
            });
            prune(*state);
            done = state->entries.empty() && state->recordings.empty();
        }
        if (done) { *link = state->next; delete state; ++released; }
        else link = &state->next;
    }
    collect_records();
    return released;
}
std::size_t orphan_count() noexcept {
    Lock registry(registry_lock);
    std::size_t count{};
    for (auto* state = orphans; state; state = state->next) ++count;
    return count;
}
} // namespace w3vr::modern_dlss_retirement
