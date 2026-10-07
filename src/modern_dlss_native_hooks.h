#pragma once
#include "modern_dlss_ownership.h"

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

// Command/queue are independently acquired native DIRECT endpoints. The
// queue supplied here ONLY locates a native function; each Execute callback
// acquires its ACTUAL queue. Never replaces a game's queue with this object.
// Host initializes MinHook and serializes install/uninstall with its other
// hook mutations. An existing legacy/third-party hook is a conflict, not proof.
bool install(const modern_dlss_ownership::OwnedCommand&,
    const modern_dlss_ownership::OwnedQueue&,
    modern_dlss_ownership::Classify, Observer) noexcept;
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
