#pragma once
#include "modern_dlss_ownership.h"
#include <atomic>
#include <memory>

namespace w3vr::modern_dlss_native_hooks {
enum class Moment { Before, After };
enum class Operation { Close, Reset };
struct Observer {
    void* context{};
    // Before has E_PENDING, not a fabricated successful return. Native
    // references live through both callbacks and the single original call.
    void (*command)(void*, const modern_dlss_ownership::OwnedCommand&,
        Operation, Moment, HRESULT) noexcept {};
    // These are the ORIGINAL native array/count at this endpoint. Borrowed
    // array is valid only during the callback; tracking must acquire its IDs.
    void (*execute)(void*, const modern_dlss_ownership::OwnedQueue&, Moment,
        UINT, ID3D12CommandList* const*) noexcept {};
};
struct Stats { std::uint64_t close{}, reset{}, execute{}, before{}, after{}; };
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using ExecuteFn = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
struct ForwarderData;
// A cooperating owner keeps its own hook and original trampoline. Construct
// only AFTER its successful MinHook enable; deactivate and stop all calls
// BEFORE removing that owner's trampoline. This is not a third-party bypass.
class Forwarder {
public:
    Forwarder() = default;
    Forwarder(Forwarder&&) noexcept;
    Forwarder& operator=(Forwarder&&) noexcept;
    Forwarder(const Forwarder&) = delete;
    Forwarder& operator=(const Forwarder&) = delete;
    ~Forwarder();
    explicit operator bool() const noexcept;
    void deactivate() noexcept;
    HRESULT reset(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*) const;
    void execute(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*) const;
private:
    std::atomic<std::shared_ptr<ForwarderData>> data_;
    friend Forwarder reset_forwarder(void*, ResetFn) noexcept;
    friend Forwarder execute_forwarder(void*, ExecuteFn) noexcept;
    friend struct Delegates;
};
Forwarder reset_forwarder(void* native_target, ResetFn original) noexcept;
Forwarder execute_forwarder(void* native_target, ExecuteFn original) noexcept;
struct Delegates {
    const Forwarder* reset{};
    const Forwarder* execute{};
    std::shared_ptr<ForwarderData> for_site(std::size_t) const noexcept;
};

// Command/queue are independently acquired native DIRECT endpoints. The
// queue supplied here ONLY locates a native function; each Execute callback
// acquires its ACTUAL queue. Never replaces a game's queue with this object.
// Host initializes MinHook and serializes install/uninstall with its other
// hook mutations. A known cooperating owner can supply a Forwarder; an
// unknown existing hook is still a conflict and cannot be silently bypassed.
bool install(const modern_dlss_ownership::OwnedCommand&,
    const modern_dlss_ownership::OwnedQueue&,
    modern_dlss_ownership::Classify, Observer, Delegates = {}) noexcept;
bool ready() noexcept;
// Disable all before removing trampolines. Removal ALSO requires a proven
// host barrier stopping ALL recording/submission calls, including threads
// between the native jump and our entry counter. Counter zero alone is not
// proof. Default false only disables/retains previously activated trampolines.
// Busy/failed cleanup retains them; retry before unloading this DLL/MinHook.
// Never call from DllMain, an observer, or under a game/SDK recording lock.
bool uninstall(bool native_calls_quiescent = false) noexcept;
Stats stats() noexcept;
} // namespace w3vr::modern_dlss_native_hooks
