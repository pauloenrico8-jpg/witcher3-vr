#include "modern_dlss_ownership.h"
#include <cstdlib>
#include <cstdio>

namespace m = w3vr::modern_dlss_ownership;
using m::Owner;
void require(bool value) { if (!value) std::abort(); }

// IUnknown doubles cover only ownership and the public SDK output contract.
// They never stand in for an executing D3D12 list or a completed game image.
struct Node : IUnknown {
    Owner owner{Owner::Native};
    Node* inner{};
    Node* graphics_result{};
    ULONG refs{1};
    unsigned queries{}, sdk_calls{};
    int sdk_status{};
    bool sdk_null{}, query_fail{}, query_null{}, resource{}, modern_base{}, query_fault{}, queue{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        ++queries; *output = nullptr;
        if (query_fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (query_fail) return E_NOINTERFACE;
        if (query_null) return S_OK;
        Node* target{};
        if (owner == Owner::ReShade && iid == w3vr::command_list_identity::kReShadeBase) target = inner;
        if (owner == Owner::Streamline && modern_base && iid == w3vr::command_list_identity::kStreamlineBase) target = inner;
        if (owner == Owner::Native && iid == (queue ? __uuidof(ID3D12CommandQueue) : (resource
            ? __uuidof(ID3D12Resource) : __uuidof(ID3D12GraphicsCommandList))))
            target = graphics_result ? graphics_result : this;
        if (!target) return E_NOINTERFACE;
        target->AddRef(); *output = target;
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { require(refs > 1); return --refs; }
};
Owner classify(const IUnknown* value) { return static_cast<const Node*>(value)->owner; }
bool verified(const IUnknown* value) { return static_cast<const Node*>(value)->modern_base; }
int sdk(void* input, void** output) {
    auto* node = static_cast<Node*>(input); ++node->sdk_calls;
    *output = nullptr;
    if (!node->sdk_null && node->inner) {
        node->inner->AddRef(); *output = node->inner;
    }
    return node->sdk_status;
}

int main() {
    const auto iid = __uuidof(ID3D12GraphicsCommandList);
    const m::StreamlineAccess enabled{sdk, 1, true};
    Node native, sl, reshade, unknown, renderdoc;
    sl.owner = Owner::Streamline; sl.inner = &native;
    reshade.owner = Owner::ReShade; reshade.inner = &native;
    unknown.owner = Owner::Unknown; renderdoc.owner = Owner::RenderDoc;
    require(m::resolve_native(nullptr, iid, classify).failure == m::Failure::Input);
    require(m::resolve_native(&native, iid, nullptr).failure == m::Failure::Input);
    {
        auto resolved = m::resolve_native(&native, iid, classify);
        require(bool(resolved) && resolved.object.Get() == &native && native.refs == 2);
        auto moved = std::move(resolved.object);
        require(!resolved.object && native.refs == 2);
    }
    require(native.refs == 1);
    require(m::resolve_native(&unknown, iid, classify).failure == m::Failure::UnknownOwner);
    require(m::resolve_native(&renderdoc, iid, classify).failure == m::Failure::UnsupportedOwner);
    require(unknown.queries == 0 && renderdoc.queries == 0);
    for (const auto gate : {m::StreamlineAccess{}, m::StreamlineAccess{sdk, 0, true},
        m::StreamlineAccess{sdk, 1, false}, m::StreamlineAccess{nullptr, 1, true}}) {
        require(m::resolve_native(&sl, iid, classify, gate).failure == m::Failure::SdkUnavailable);
        require(sl.sdk_calls == 0 && sl.queries == 0);
    }
    sl.inner = &reshade;
    {
        auto r = m::resolve_native(&sl, iid, classify, enabled);
        require(bool(r) && r.wrappers == 2 && r.object.Get() == &native);
        require(native.refs == 2 && sl.refs == 1 && reshade.refs == 1);
    }
    reshade.inner = &sl; sl.inner = &native;
    {
        auto r = m::resolve_native(&reshade, iid, classify, enabled);
        require(bool(r) && r.wrappers == 2 && native.refs == 2);
    }
    require(native.refs == 1 && sl.refs == 1 && reshade.refs == 1);
    const m::StreamlineAccess modern_qi{nullptr, 0, false, verified};
    sl.modern_base = true;
    const auto calls_before = sl.sdk_calls;
    {
        auto resolved = m::resolve_native(&reshade, iid, classify, modern_qi);
        require(bool(resolved) && resolved.wrappers == 2 && native.refs == 2);
    }
    require(sl.sdk_calls == calls_before && native.refs == 1);
    sl.modern_base = false;
    require(m::resolve_native(&sl, iid, classify, modern_qi).failure == m::Failure::SdkUnavailable);
    // A model's vtable is never accepted as the actual installed component.
    require(!m::installed_streamline_command_base(&sl));
    require(!m::installed_streamline_queue_base(&sl));
    native.query_fault = true;
    require(m::resolve_native(&native, iid, classify).failure == m::Failure::Interface);
    native.query_fault = false;
    sl.sdk_status = 7;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::SdkError);
    require(native.refs == 1); // Also releases an error's non-null owned output.
    sl.sdk_status = 0; sl.sdk_null = true;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::SdkError);
    sl.sdk_null = false;
    // The installed SDK's successful same-input fallback must not be trusted.
    sl.inner = &sl;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::Cycle);
    require(sl.refs == 1);
    sl.inner = &reshade; reshade.inner = &sl;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::Cycle);
    require(sl.refs == 1 && reshade.refs == 1);
    sl.inner = &unknown;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::UnknownOwner);
    require(unknown.refs == 1 && unknown.queries == 0);
    sl.inner = &native; native.query_null = true;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::Interface);
    native.query_null = false; native.query_fail = true;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::Interface);
    native.query_fail = false; native.graphics_result = &unknown;
    require(m::resolve_native(&sl, iid, classify, enabled).failure == m::Failure::Interface);
    require(unknown.refs == 1 && native.refs == 1);
    native.graphics_result = nullptr;
    std::array<Node, w3vr::command_list_identity::kMaxWrappers + 1> chain;
    for (std::size_t i = 0; i < chain.size(); ++i) {
        chain[i].owner = i & 1 ? Owner::Streamline : Owner::ReShade;
        chain[i].inner = i + 1 < chain.size() ? &chain[i + 1] : &native;
    }
    require(m::resolve_native(&chain[0], iid, classify, enabled).failure == m::Failure::Depth);
    {
        auto r = m::resolve_native(&chain[1], iid, classify, enabled);
        require(bool(r) && r.wrappers == 8 && native.refs == 2);
    }
    for (const auto& n : chain) require(n.refs == 1);
    Node texture; texture.resource = true;
    {
        auto r = m::resolve_native(&texture, __uuidof(ID3D12Resource), classify);
        require(bool(r) && texture.refs == 2);
    }
    require(texture.refs == 1);
    require(m::resolve_native(&texture, iid, classify).failure == m::Failure::Interface);
    Node queue; queue.queue = true;
    {
        auto resolved = m::resolve_native(&queue, __uuidof(ID3D12CommandQueue), classify);
        require(bool(resolved) && queue.refs == 2);
    }
    require(queue.refs == 1);
    require(m::resolve_native(&queue, iid, classify).failure == m::Failure::Interface);
    require(m::acquire_queue(nullptr, classify).failure == m::Failure::Input);
    require(!m::compatible_queue_device({}, {}));
    // Empty/forged receipts never enter COM. Opaque tokens are not read.
    w3vr::engine_dlss_resources::CpuResourceReceipt receipt;
    require(m::acquire(receipt, classify).failure == m::Failure::Receipt);
    require(!m::fence_reached(0, 1) && !m::fence_reached(4, 5));
    require(m::fence_reached(5, 5) && m::fence_reached(6, 5));
    require(!m::fence_reached(UINT64_MAX, 5));
    require(!m::fence_reached(5, 0) && !m::fence_reached(UINT64_MAX, UINT64_MAX));
    require(native.refs == 1);
    std::puts("PASS modern native identity, strict SDK fallback rejection and COM reference balance");
}
