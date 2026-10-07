#pragma once

#include "command_list_identity.h"
#include "engine_dlss_resources.h"
#include <wrl/client.h>

namespace w3vr::modern_dlss_ownership {

using Microsoft::WRL::ComPtr;
using command_list_identity::Owner;
using Classify = command_list_identity::ClassifyOwner;

enum class Failure {
    None, Input, UnknownOwner, UnsupportedOwner, SdkUnavailable, SdkError,
    Query, Cycle, Depth, Interface, Receipt, Device, AliasedResource, Texture
};

// Installed 2.14.1 returns an added reference, including its same-input
// fallback. It is NOT thread safe. A non-null function alone is insufficient:
// the host must prove initialization/lifetime and serialize with its SDK use.
// This capability remains absent in the game hook until those are verified.
using GetNativeInterface = int (*)(void*, void**);
using VerifyBaseQuery = bool (*)(const IUnknown*);
struct StreamlineAccess {
    GetNativeInterface function{};
    std::uint64_t initialized_epoch{};
    bool serialized_host_call{};
    VerifyBaseQuery verified_base_query{};
};

// Independently verified installed command-list QI. Checks module-relative
// vtable/slots/GUIDs and the entire QI function digest in loaded memory.
// Hooked, changed, unloaded or other proxy classes do not get this capability.
bool installed_streamline_command_base(const IUnknown*) noexcept;
// A separate installed queue-class proof. The command-list profile must NOT
// be used to authorize QI on a queue (or vice versa).
bool installed_streamline_queue_base(const IUnknown*) noexcept;

struct NativeInterface {
    ComPtr<IUnknown> object; // Owns the requested interface, not a borrowed key.
    std::uint32_t wrappers{};
    Failure failure{Failure::None};
    explicit operator bool() const { return object && failure == Failure::None; }
};

// Native endpoints only. RenderDoc/unknown objects are rejected. Streamline
// uses separately verified modern base QI, or its gated two-argument public
// API. Never assumes the legacy marker works just because the DLL is named SL.
// ReShade uses its already checked unwrapped-object QI contract. Every
// intermediate added reference is released, even on cycles and SDK errors.
NativeInterface resolve_native(IUnknown*, REFIID, Classify,
    const StreamlineAccess& = {}) noexcept;

struct OwnedEvaluation {
    engine_dlss::Identity identity{};
    std::uint32_t viewport{UINT32_MAX};
    ComPtr<ID3D12GraphicsCommandList> command;
    ComPtr<IUnknown> command_identity;
    ComPtr<ID3D12Device> device;
    ComPtr<IUnknown> device_identity;
    std::array<ComPtr<ID3D12Resource>, 4> resources;
    std::array<ComPtr<IUnknown>, 4> resource_identities;
    std::array<D3D12_RESOURCE_DESC, 4> descriptions{};
    Failure failure{Failure::None};
    explicit operator bool() const { return command && failure == Failure::None; }
    // COM references prove object lifetime, not immutable contents, Reset
    // generation, queue submission, fence retirement, or an image for VR.
    static constexpr bool gpu_completion_verified = false;
};

// Call only while the native callback still keeps the input objects alive.
// On failure no partial ownership escapes. This never executes/changes the
// command list, transitions a texture, calls the SDK without a capability,
// or changes native options/tags/history.
OwnedEvaluation acquire(const engine_dlss_resources::CpuResourceReceipt&,
    Classify, const StreamlineAccess& = {});

struct OwnedQueue {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IUnknown> queue_identity;
    ComPtr<ID3D12Device> device;
    ComPtr<IUnknown> device_identity;
    Failure failure{Failure::None};
    explicit operator bool() const { return queue && failure == Failure::None; }
    static constexpr bool gpu_completion_verified = false;
};

// Acquire while the real submission callback still owns its queue. Only a
// DIRECT native endpoint is admitted. Never substitute the swapchain queue,
// call Execute/Signal, or infer submission from matching devices alone.
OwnedQueue acquire_queue(IUnknown*, Classify, const StreamlineAccess& = {});
bool compatible_queue_device(const OwnedQueue&, const OwnedEvaluation&) noexcept;

// UINT64_MAX is D3D12's device-removal sentinel, never a completed image.
bool fence_reached(std::uint64_t completed, std::uint64_t target) noexcept;

} // namespace w3vr::modern_dlss_ownership
