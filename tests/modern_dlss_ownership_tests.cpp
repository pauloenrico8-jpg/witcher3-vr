#include "modern_dlss_ownership.h"
#include <cstdlib>
#include <cstdio>
#include <MinHook.h>
#include <cstring>

namespace m = w3vr::modern_dlss_ownership;
using m::Owner;
unsigned checks{};
void require(bool value) { ++checks;if (!value) std::abort(); }

// IUnknown doubles cover only ownership and the public SDK output contract.
// They never stand in for an executing D3D12 list or a completed game image.
struct Node : IUnknown {
    Owner owner{Owner::Native};
    Node* inner{};
    Node* graphics_result{};
    Node* identity_result{};
    ULONG refs{1};
    unsigned queries{}, sdk_calls{};
    int sdk_status{};
    bool sdk_null{}, query_fail{}, query_null{}, resource{}, modern_base{}, query_fault{}, queue{}, device{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        ++queries; *output = nullptr;
        if (query_fault) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        if (query_fail) return E_NOINTERFACE;
        if (query_null) return S_OK;
        Node* target{};
        if (owner == Owner::ReShade && iid == w3vr::command_list_identity::kReShadeBase) target = inner;
        if (owner == Owner::Streamline && modern_base && iid == w3vr::command_list_identity::kStreamlineBase) target = inner;
        if(owner==Owner::Native && device && iid==__uuidof(IUnknown))target=identity_result?identity_result:this;
        if (owner == Owner::Native && iid == (queue ? __uuidof(ID3D12CommandQueue) : (device ? __uuidof(ID3D12Device) : (resource
            ? __uuidof(ID3D12Resource) : __uuidof(ID3D12GraphicsCommandList)))))
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

// These are table-only fixtures. Even the matching fixture is NOT a live SDK
// Device. They only exercise the loaded-image profile guard; no QI is invoked.
unsigned profile_qi_calls{};
HRESULT STDMETHODCALLTYPE profile_qi_never_called(IUnknown*,REFIID,void**){++profile_qi_calls;return E_UNEXPECTED;}
void loaded_device_profile(const wchar_t* path){
    require(GetModuleHandleW(L"sl.interposer.dll")==nullptr);
    const auto module=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    require(module && GetModuleHandleW(L"sl.interposer.dll")==module);
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    struct TableFixture{const std::uintptr_t* table;} fixture{reinterpret_cast<const std::uintptr_t*>(base+0x788C0)};
    auto object=[&](){return reinterpret_cast<const IUnknown*>(&fixture);};
    require(m::installed_streamline_device_base(object()));
    require(!m::installed_streamline_command_base(object()) && !m::installed_streamline_queue_base(object()));
    for(auto other:{0x782F0,0x78588,0x79348,0x794A8}){
        fixture.table=reinterpret_cast<const std::uintptr_t*>(base+other);
        require(!m::installed_streamline_device_base(object()));
    }
    fixture.table=reinterpret_cast<const std::uintptr_t*>(base+0x788C0);
    std::array<std::uintptr_t,16> copied{};memcpy(copied.data(),fixture.table,sizeof(copied));
    fixture.table=copied.data();require(!m::installed_streamline_device_base(object()));
    fixture.table=reinterpret_cast<const std::uintptr_t*>(base+0x788C0);
    require(MH_Initialize()==MH_OK);void* original{};auto* target=reinterpret_cast<void*>(base+0x252C0);
    require(MH_CreateHook(target,reinterpret_cast<void*>(&profile_qi_never_called),&original)==MH_OK && original);
    require(m::installed_streamline_device_base(object())); // Create alone did not alter entry bytes.
    require(MH_EnableHook(target)==MH_OK);
    require(!m::installed_streamline_device_base(object()) && profile_qi_calls==0);
    require(MH_DisableHook(target)==MH_OK && MH_RemoveHook(target)==MH_OK);
    require(MH_Uninitialize()==MH_OK && m::installed_streamline_device_base(object()));
    // The probe has no threads executing SDK methods. Removal here is not an
    // all-call/loader/unload barrier for the Witcher host.
    require(profile_qi_calls==0 && FreeLibrary(module));
    require(GetModuleHandleW(L"sl.interposer.dll")==nullptr && !m::installed_streamline_device_base(object()));
    std::puts("PASS loaded installed device profile: matching table-only fixture, wrong classes, copied table, real MinHook byte change/restoration, sequential unload rejection; SDK Device object/QI/initialization NOT executed");
}

int wmain(int argc,wchar_t** argv) {
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
    require(!m::installed_streamline_device_base(nullptr));
    require(!m::installed_streamline_device_base(&sl));
    require(!m::installed_streamline_device_base(&native));

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
    require(m::acquire_command(nullptr, classify).failure == m::Failure::Input);
    const auto unknown_queries = unknown.queries;
    require(m::acquire_command(&unknown, classify).failure == m::Failure::UnknownOwner &&
        unknown.queries == unknown_queries && unknown.refs == 1);
    require(!m::compatible_queue_device({}, {}));
    // Device doubles model only QI/reference ownership, never D3D12 methods.
    const auto device_sdk_before=sl.sdk_calls;
    Node device;device.device=true;
    require(m::acquire_device(nullptr,classify).failure==m::Failure::Input);
    require(m::acquire_device(&device,nullptr).failure==m::Failure::Input);
    require(m::acquire_device(&native,classify).failure==m::Failure::Interface && native.refs==1);
    {
        auto owned=m::acquire_device(&device,classify);
        require(bool(owned) && owned.device.Get()==reinterpret_cast<ID3D12Device*>(&device) &&
            owned.device_identity.Get()==&device && device.refs==3);
        auto moved=std::move(owned.device);require(!owned.device && device.refs==3);
    }
    require(device.refs==1);
    device.identity_result=&unknown;
    require(m::acquire_device(&device,classify).failure==m::Failure::Interface && device.refs==1 && unknown.refs==1);
    device.identity_result=nullptr;
    sl.inner=&device;sl.modern_base=true;
    {
        auto owned=m::acquire_device(&sl,classify,modern_qi);
        require(bool(owned) && device.refs==3 && sl.refs==1 && sl.sdk_calls==device_sdk_before);
    }
    require(device.refs==1);
    // A closed nested ReShade policy rejects before touching its QI; neither
    // same SDK name nor the model's class grants a production device proof.
    sl.inner=&reshade;reshade.inner=&device;
    const auto no_nested=m::StreamlineAccess{nullptr,0,false,verified,false};
    const auto reshade_queries=reshade.queries;
    require(m::acquire_device(&sl,classify,no_nested).failure==m::Failure::UnsupportedOwner &&
        reshade.queries==reshade_queries && sl.refs==1 && reshade.refs==1 && device.refs==1);
    void* output=reinterpret_cast<void*>(1);
    require(m::resolve_binding_endpoint(&device,__uuidof(ID3D12Device),nullptr,classify)==E_POINTER);
    require(m::resolve_binding_endpoint(nullptr,__uuidof(ID3D12Device),&output,classify)==E_INVALIDARG && !output);
    require(m::resolve_binding_endpoint(&device,__uuidof(ID3D12Device),&output,classify)==S_OK && output==&device && device.refs==2);
    static_cast<IUnknown*>(output)->Release();require(device.refs==1);
    const auto device_queries=device.queries;
    require(FAILED(m::resolve_binding_endpoint(&device,__uuidof(ID3D12Resource),&output,classify)) && !output && device.queries==device_queries);
    for(Node* denied:{&unknown,&renderdoc,&reshade,&sl}){
        const auto before=denied->queries;
        require(FAILED(m::resolve_binding_endpoint(denied,__uuidof(ID3D12Device),&output,classify)) && !output && denied->queries==before && denied->refs==1);
    }
    require(sl.sdk_calls==device_sdk_before && device.refs==1);
    sl.inner=&native;sl.modern_base=false;reshade.inner=&native;
    std::puts("PASS typed device binding: modeled QI ownership/failure balance and class-policy rejection; no installed Streamline runtime or D3D12 device methods executed");
    // Empty/forged receipts never enter COM. Opaque tokens are not read.
    w3vr::engine_dlss_resources::CpuResourceReceipt receipt;
    require(m::acquire(receipt, classify).failure == m::Failure::Receipt);
    require(!m::fence_reached(0, 1) && !m::fence_reached(4, 5));
    require(m::fence_reached(5, 5) && m::fence_reached(6, 5));
    require(!m::fence_reached(UINT64_MAX, 5));
    require(!m::fence_reached(5, 0) && !m::fence_reached(UINT64_MAX, UINT64_MAX));
    require(native.refs == 1);
    std::puts("PASS modern native identity, strict SDK fallback rejection and COM reference balance");
    if(argc==2)loaded_device_profile(argv[1]);
    else require(argc==1);
    std::printf("PASS CPU ownership checks=%u; device objects and verification capabilities above are modeled\n",checks);
}
