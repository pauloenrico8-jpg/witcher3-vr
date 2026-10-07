#include "modern_dlss_ownership.h"
#include <bcrypt.h>

namespace w3vr::modern_dlss_ownership {
namespace {
HRESULT query(IUnknown* input, REFIID iid, void** output) noexcept {
    *output = nullptr;
    __try { return input->QueryInterface(iid, output); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *output = nullptr; return E_FAIL; }
}

bool identity(IUnknown* input, Classify classify, ComPtr<IUnknown>& output) {
    IUnknown* pointer{};
    const HRESULT result = query(input, __uuidof(IUnknown),
        reinterpret_cast<void**>(&pointer));
    ComPtr<IUnknown> owned;
    owned.Attach(pointer);
    if (FAILED(result) || !owned || classify(owned.Get()) != Owner::Native) return false;
    output = std::move(owned);
    return true;
}

template<class T> ComPtr<T> take(NativeInterface& result) {
    ComPtr<T> output;
    output.Attach(static_cast<T*>(result.object.Detach()));
    return output;
}

bool valid_receipt(const engine_dlss_resources::CpuResourceReceipt& input) {
    namespace r = engine_dlss_resources;
    if (!input.valid || !input.identity.valid() || input.command == nullptr ||
        input.token == nullptr || input.viewport != engine_dlss::constructed_viewport) return false;
    constexpr std::array<std::uint32_t, 4> types{0, 1, 3, 4};
    for (std::size_t i = 0; i < input.bindings.size(); ++i) {
        const auto& b = input.bindings[i];
        if (b.tag.type != types[i] || !r::address(b.tag.resource) ||
            b.tag.lifecycle != 1 || !r::address(b.resource.native) ||
            b.resource.memory != 0 || b.resource.view != 0 ||
            b.resource.state == UINT32_MAX || b.tag.extent.left != 0 ||
            b.tag.extent.top != 0 || b.tag.extent.width == 0 ||
            b.tag.extent.height == 0) return false;
    }
    const auto& b = input.bindings;
    return b[0].tag.extent == b[1].tag.extent && b[0].tag.extent == b[2].tag.extent &&
        b[1].resource.state == 0 && b[3].resource.state == 8 &&
        b[3].tag.extent.width >= b[2].tag.extent.width &&
        b[3].tag.extent.height >= b[2].tag.extent.height;
}
} // namespace

bool installed_streamline_command_base(const IUnknown* object) noexcept {
    // These sites are independently tied to the installed 2.14.1 binary,
    // command-list class GUID and public QI branch. No private field is read.
    // Digest checks all 179 bytes, including both GUID comparisons, added
    // reference, return path and unknown-interface fallback. Do not cache a
    // positive result across unloading/reinitialization or hook changes.
    constexpr std::array<unsigned char, 32> expected{
        0xb3,0xd0,0xfd,0xe6,0xf9,0xae,0x01,0x73,0xc1,0x0d,0xd9,0x26,0x16,0x1a,0x7e,0x8a,
        0xa9,0xc7,0x4a,0x81,0x0d,0xec,0x2a,0x42,0x7c,0xcd,0xdc,0xdf,0xaf,0x5c,0xac,0x6f};
    constexpr GUID command_type{0x5B2662FB,0xEB28,0x4AEC,{0x81,0x9E,0x1C,0x1B,0x4D,0xE0,0x60,0xF6}};
    if (!object) return false;
    const auto module = GetModuleHandleW(L"sl.interposer.dll");
    if (!module) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    __try {
        const auto table = *reinterpret_cast<const std::uintptr_t* const*>(object);
        if (reinterpret_cast<std::uintptr_t>(table) != base + 0x782F0 ||
            table[0] != base + 0x23400 || table[1] != base + 0x234C0 || table[2] != base + 0x234F0 ||
            table[7] != base + 0x23720 || table[8] != base + 0x23730 ||
            table[9] != base + 0x23740 || table[10] != base + 0x23750 ||
            reinterpret_cast<std::uintptr_t>(GetProcAddress(module, "slGetNativeInterface")) != base + 0x7EA0 ||
            *reinterpret_cast<const GUID*>(base + 0x778F0) != command_list_identity::kStreamlineBase ||
            *reinterpret_cast<const GUID*>(base + 0x77938) != command_type) return false;
        std::array<unsigned char, 32> actual{};
        const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
            reinterpret_cast<unsigned char*>(base + 0x23400), 179,
            actual.data(), static_cast<ULONG>(actual.size()));
        return status >= 0 && actual == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool installed_streamline_queue_base(const IUnknown* object) noexcept {
    constexpr std::array<unsigned char, 32> expected{
        0xac,0xa5,0x53,0xa4,0x52,0x66,0x88,0xd6,0x6f,0x1b,0x90,0x2c,0x1d,0x38,0x27,0xc8,
        0x05,0xa1,0x40,0x54,0x21,0xc0,0xa6,0x5b,0xde,0x15,0xc9,0x9f,0x64,0x82,0x59,0xbe};
    constexpr GUID queue_type{0x22C3768E,0xAB10,0x4870,{0xB0,0x3B,0x2B,0x52,0xE2,0x1B,0x10,0x63}};
    if (!object) return false;
    const auto module = GetModuleHandleW(L"sl.interposer.dll");
    if (!module) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    __try {
        const auto table = *reinterpret_cast<const std::uintptr_t* const*>(object);
        if (reinterpret_cast<std::uintptr_t>(table) != base + 0x78588 ||
            table[0] != base + 0x24BC0 || table[1] != base + 0x24C80 || table[2] != base + 0x24CB0 ||
            table[7] != base + 0x24CF0 || table[10] != base + 0x24D20 ||
            table[14] != base + 0x238A0 || table[15] != base + 0x24F20 || table[18] != base + 0x24F40 ||
            reinterpret_cast<std::uintptr_t>(GetProcAddress(module, "slGetNativeInterface")) != base + 0x7EA0 ||
            *reinterpret_cast<const GUID*>(base + 0x778F0) != command_list_identity::kStreamlineBase ||
            *reinterpret_cast<const GUID*>(base + 0x77738) != queue_type) return false;
        std::array<unsigned char, 32> actual{};
        const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
            reinterpret_cast<unsigned char*>(base + 0x24BC0), 179,
            actual.data(), static_cast<ULONG>(actual.size()));
        return status >= 0 && actual == expected;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

NativeInterface resolve_native(IUnknown* input, REFIID iid, Classify classify,
    const StreamlineAccess& sdk) noexcept {
    NativeInterface result{};
    if (input == nullptr || classify == nullptr) {
        result.failure = Failure::Input;
        return result;
    }
    std::array<ComPtr<IUnknown>, command_list_identity::kMaxWrappers> references;
    IUnknown* current = input;
    for (;;) {
        const auto owner = classify(current);
        if (owner == Owner::Native) {
            IUnknown* requested{};
            const HRESULT hr = query(current, iid, reinterpret_cast<void**>(&requested));
            ComPtr<IUnknown> owned;
            owned.Attach(requested);
            if (FAILED(hr) || !owned || classify(owned.Get()) != Owner::Native) {
                result.failure = Failure::Interface;
                return result;
            }
            result.object = std::move(owned);
            return result;
        }
        if (owner != Owner::Streamline && owner != Owner::ReShade) {
            result.failure = owner == Owner::Unknown
                ? Failure::UnknownOwner : Failure::UnsupportedOwner;
            return result;
        }
        if (result.wrappers == references.size()) {
            result.failure = Failure::Depth;
            return result;
        }
        IUnknown* next{};
        bool accepted{};
        if (owner == Owner::Streamline) {
            if (sdk.verified_base_query && sdk.verified_base_query(current)) {
                // This exact modern QI branch only returns/AddRefs the base;
                // it does not query SDK initialization or mutate proxy state.
                accepted = SUCCEEDED(query(current, command_list_identity::kStreamlineBase,
                    reinterpret_cast<void**>(&next))) && next != nullptr;
                result.failure = Failure::Query;
            } else if (!sdk.function || sdk.initialized_epoch == 0 || !sdk.serialized_host_call) {
                result.failure = Failure::SdkUnavailable;
                return result;
            } else {
                void* output{};
                const int status = sdk.function(current, &output);
                next = static_cast<IUnknown*>(output);
                accepted = status == 0 && next != nullptr;
                result.failure = Failure::SdkError;
            }
        } else {
            accepted = SUCCEEDED(query(current, command_list_identity::kReShadeBase,
                reinterpret_cast<void**>(&next))) && next != nullptr;
            result.failure = Failure::Query;
        }
        ComPtr<IUnknown> owned;
        owned.Attach(next);
        if (!accepted) return result;
        bool cycle = next == input || next == current;
        for (std::uint32_t i = 0; i < result.wrappers; ++i)
            cycle = cycle || next == references[i].Get();
        if (cycle) {
            result.failure = Failure::Cycle;
            return result;
        }
        references[result.wrappers++] = std::move(owned);
        current = next;
        result.failure = Failure::None;
    }
}

OwnedCommand acquire_command(IUnknown* input, Classify classify, const StreamlineAccess& sdk) {
    auto failed = [](Failure reason) {
        OwnedCommand output{};
        output.failure = reason;
        return output;
    };
    OwnedCommand candidate{};
    auto command = resolve_native(input,
        __uuidof(ID3D12GraphicsCommandList), classify, sdk);
    if (!command) return failed(command.failure);
    candidate.command = take<ID3D12GraphicsCommandList>(command);
    if (!identity(candidate.command.Get(), classify, candidate.command_identity))
        return failed(Failure::Interface);
    // Only the observed DIRECT path. Compute/bundle/copy lists need separate
    // queue/lifetime contracts; do not infer one from a successful QI.
    if (candidate.command->GetType() != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return failed(Failure::Interface);
    ComPtr<ID3D12Device> command_device;
    if (FAILED(candidate.command->GetDevice(IID_PPV_ARGS(&command_device))) ||
        !command_device || classify(command_device.Get()) != Owner::Native ||
        !identity(command_device.Get(), classify, candidate.device_identity))
        return failed(Failure::Device);
    candidate.device = std::move(command_device);
    return candidate;
}

OwnedEvaluation acquire(const engine_dlss_resources::CpuResourceReceipt& input,
    Classify classify, const StreamlineAccess& sdk) {
    auto failed = [](Failure reason) {
        OwnedEvaluation output{}; output.failure = reason; return output;
    };
    if (classify == nullptr || !valid_receipt(input)) return failed(Failure::Receipt);
    OwnedEvaluation candidate{};
    auto command = acquire_command(static_cast<IUnknown*>(input.command), classify, sdk);
    if (!command) return failed(command.failure);
    static_cast<OwnedCommand&>(candidate) = std::move(command);
    for (std::size_t i = 0; i < candidate.resources.size(); ++i) {
        auto resource = resolve_native(reinterpret_cast<IUnknown*>(
            input.bindings[i].resource.native), __uuidof(ID3D12Resource), classify, sdk);
        if (!resource) return failed(resource.failure);
        candidate.resources[i] = take<ID3D12Resource>(resource);
        if (!identity(candidate.resources[i].Get(), classify, candidate.resource_identities[i]))
            return failed(Failure::Interface);
        for (std::size_t j = 0; j < i; ++j)
            if (candidate.resource_identities[i].Get() == candidate.resource_identities[j].Get())
                return failed(Failure::AliasedResource);
        ComPtr<ID3D12Device> device;
        ComPtr<IUnknown> device_identity;
        if (FAILED(candidate.resources[i]->GetDevice(IID_PPV_ARGS(&device))) || !device ||
            classify(device.Get()) != Owner::Native || !identity(device.Get(), classify, device_identity) ||
            device_identity.Get() != candidate.device_identity.Get()) return failed(Failure::Device);
        auto desc = candidate.resources[i]->GetDesc();
        const auto extent = input.bindings[i].tag.extent;
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Format == DXGI_FORMAT_UNKNOWN ||
            desc.Width < extent.width || desc.Height < extent.height ||
            desc.DepthOrArraySize != 1 || desc.MipLevels == 0 ||
            desc.SampleDesc.Count != 1 || desc.SampleDesc.Quality != 0 ||
            (i == 3 && (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0))
            return failed(Failure::Texture);
        candidate.descriptions[i] = desc;
    }
    candidate.identity = input.identity;
    candidate.viewport = input.viewport;
    return candidate;
}

OwnedQueue acquire_queue(IUnknown* input, Classify classify, const StreamlineAccess& sdk) {
    auto failed = [](Failure reason) {
        OwnedQueue result{}; result.failure = reason; return result;
    };
    auto native = resolve_native(input, __uuidof(ID3D12CommandQueue), classify, sdk);
    if (!native) return failed(native.failure);
    OwnedQueue candidate;
    candidate.queue = take<ID3D12CommandQueue>(native);
    if (!identity(candidate.queue.Get(), classify, candidate.queue_identity))
        return failed(Failure::Interface);
    if (candidate.queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        return failed(Failure::Interface);
    if (FAILED(candidate.queue->GetDevice(IID_PPV_ARGS(&candidate.device))) ||
        !candidate.device || classify(candidate.device.Get()) != Owner::Native ||
        !identity(candidate.device.Get(), classify, candidate.device_identity))
        return failed(Failure::Device);
    return candidate;
}

bool compatible_queue_device(const OwnedQueue& queue, const OwnedEvaluation& evaluation) noexcept {
    return queue && evaluation && queue.device_identity && evaluation.device_identity &&
        queue.device_identity.Get() == evaluation.device_identity.Get();
}

bool fence_reached(std::uint64_t completed, std::uint64_t target) noexcept {
    return target != 0 && target != UINT64_MAX && completed != UINT64_MAX && completed >= target;
}
} // namespace w3vr::modern_dlss_ownership
