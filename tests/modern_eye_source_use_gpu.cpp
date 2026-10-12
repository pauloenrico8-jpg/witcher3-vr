#include "modern_eye_source_use.h"
#include "modern_eye_resource_set.h"
#include "modern_graphics_binding.h"
#include "modern_backbuffer_source.h"
#include "modern_backbuffer_hooks.h"
#include <MinHook.h>
#include <barrier>
#include <thread>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace use=w3vr::modern_eye_source_use;
using Microsoft::WRL::ComPtr;
unsigned checks{},copies{};
void require(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
void check(HRESULT hr,const char* why){if(FAILED(hr)){std::printf("HRESULT %08lx %s\n",static_cast<unsigned long>(hr),why);throw std::runtime_error(why);}}
struct TrackedUse:use::Use {
    unsigned* destroyed{};
    ~TrackedUse(){++*destroyed;}
};
ComPtr<ID3D12Resource> buffer(ID3D12Device* device,D3D12_HEAP_TYPE type,UINT64 bytes,D3D12_RESOURCE_STATES state){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;
    desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> out;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&out)),"buffer");return out;
}
ComPtr<ID3D12Resource> texture(ID3D12Device* device,UINT extent=16){
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=extent;desc.Height=extent;
    desc.DepthOrArraySize=1;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    ComPtr<ID3D12Resource> out;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)),"texture");return out;
}
ComPtr<ID3D12CommandQueue> queue(ID3D12Device* device){
    D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> out;check(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&out)),"queue");return out;
}
ComPtr<ID3D12Fence> fence(ID3D12Device* device){
    ComPtr<ID3D12Fence> out;check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&out)),"fence");return out;
}
struct Recording {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> command;
    explicit Recording(ID3D12Device* device){
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"allocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&command)),"list");
    }
    HRESULT close(){const auto result=command->Close();check(result,"close");return result;}
};
void wait(ID3D12Fence* finished,UINT64 value){
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");
    const HRESULT result=finished->SetEventOnCompletion(value,event);
    if(FAILED(result)){CloseHandle(event);check(result,"completion event");}
    const DWORD waited=WaitForSingleObject(event,10000);CloseHandle(event);
    require(waited==WAIT_OBJECT_0,"GPU completion timeout");
    const auto completed=finished->GetCompletedValue();
    if(completed==UINT64_MAX){
        ComPtr<ID3D12Device> lost_device;
        if(SUCCEEDED(finished->GetDevice(IID_PPV_ARGS(&lost_device)))){
            std::printf("REMOVAL_REASON %08lx\n",static_cast<unsigned long>(lost_device->GetDeviceRemovedReason()));
            ComPtr<ID3D12InfoQueue> info;
            if(SUCCEEDED(lost_device.As(&info))){
                const auto count=info->GetNumStoredMessagesAllowedByRetrievalFilter();
                for(UINT64 i=0;i<count;++i){SIZE_T bytes{};info->GetMessage(i,nullptr,&bytes);
                    std::vector<unsigned char> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                    if(SUCCEEDED(info->GetMessage(i,message,&bytes)) && message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING)
                        std::printf("D3D12_MESSAGE %u %s\n",message->ID,message->pDescription);
                }
            }
        }
    }
    require(completed!=UINT64_MAX && completed>=value,"device loss/unfinished fence");
}
constexpr UINT64 stride=4096;
D3D12_TEXTURE_COPY_LOCATION placed(ID3D12Resource* resource,UINT64 offset){
    D3D12_TEXTURE_COPY_LOCATION loc{};loc.pResource=resource;loc.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    loc.PlacedFootprint.Offset=offset;loc.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    loc.PlacedFootprint.Footprint.Width=16;loc.PlacedFootprint.Footprint.Height=16;
    loc.PlacedFootprint.Footprint.Depth=1;loc.PlacedFootprint.Footprint.RowPitch=256;return loc;
}
D3D12_TEXTURE_COPY_LOCATION subresource(ID3D12Resource* resource){
    D3D12_TEXTURE_COPY_LOCATION loc{};loc.pResource=resource;loc.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;return loc;
}
ComPtr<ID3D12Resource> upload_pixels(ID3D12Device* device){
    auto upload=buffer(device,D3D12_HEAP_TYPE_UPLOAD,stride*2,D3D12_RESOURCE_STATE_GENERIC_READ);
    void* bytes{};D3D12_RANGE no_read{0,0};check(upload->Map(0,&no_read,&bytes),"upload map");
    std::memset(bytes,0,stride*2);
    for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<16;++y)
        std::memset(static_cast<unsigned char*>(bytes)+eye*stride+y*256,eye?0xC9:0x31,64);
    upload->Unmap(0,nullptr);return upload;
}
void populate(Recording& recording,ID3D12Resource* upload,const std::array<ComPtr<ID3D12Resource>,2>& textures){
    for(unsigned eye=0;eye<2;++eye){
        auto src=placed(upload,eye*stride),dst=subresource(textures[eye].Get());
        recording.command->CopyTextureRegion(&dst,0,0,0,&src,nullptr);++copies;
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=textures[eye].Get();barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        recording.command->ResourceBarrier(1,&barrier);
    }
}
void readback(Recording& recording,ID3D12Resource* destination,const std::array<ComPtr<ID3D12Resource>,2>& textures){
    for(unsigned eye=0;eye<2;++eye){auto src=subresource(textures[eye].Get()),dst=placed(destination,eye*stride);
        recording.command->CopyTextureRegion(&dst,0,0,0,&src,nullptr);++copies;
    }
}
void verify_pixels(ID3D12Resource* result,unsigned char left=0x31,unsigned char right=0xC9){
    void* bytes{};D3D12_RANGE range{0,stride*2};check(result->Map(0,&range,&bytes),"readback map");
    for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x)
        require(static_cast<unsigned char*>(bytes)[eye*stride+y*256+x]==(eye?right:left),"distinct eye pixel copy mismatch");
    D3D12_RANGE no_write{0,0};result->Unmap(0,&no_write);
}
void copy_case(ID3D12Device* device,bool cross_queue){
    auto consumer=queue(device),producer=cross_queue?queue(device):consumer;
    auto completed=fence(device),gate=fence(device),produced=fence(device);
    Recording consumer_record(device),producer_record(device);
    auto upload=upload_pixels(device);auto result=buffer(device,D3D12_HEAP_TYPE_READBACK,stride*2,D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<ComPtr<ID3D12Resource>,2> images{texture(device),texture(device)};
    unsigned destroyed{};use::Store store{};auto owned=std::make_unique<TrackedUse>();owned->destroyed=&destroyed;
    owned->textures=images;owned->queue=consumer;owned->fence=completed;
    owned->producers={producer,producer};owned->producer_fences={produced,produced};
    owned->recording=consumer_record.command;owned->allocator=consumer_record.allocator;owned->xr_destination=result;
    owned->recorded=true;auto* retained=store.adopt(std::move(owned));
    if(cross_queue){
        populate(producer_record,upload.Get(),images);producer_record.close();
        check(producer->Wait(gate.Get(),1),"blocked producer wait");
        ID3D12CommandList* array[]{producer_record.command.Get()};producer->ExecuteCommandLists(1,array);
        check(producer->Signal(produced.Get(),1),"producer signal");check(consumer->Wait(produced.Get(),1),"cross queue wait");
    }else{
        check(consumer->Wait(gate.Get(),1),"blocked consumer wait");populate(consumer_record,upload.Get(),images);
    }
    readback(consumer_record,result.Get(),images);consumer_record.close();
    ID3D12CommandList* array[]{consumer_record.command.Get()};
    ID3D12CommandList* foreign_array[]{producer_record.command.Get()};
    require(!store.before_execute(retained,consumer.Get(),0,array) &&
        !store.before_execute(retained,consumer.Get(),1,nullptr) &&
        !store.before_execute(retained,consumer.Get(),1,foreign_array),"changed submission array/count admitted");
    auto foreign_queue=queue(device);
    require(!store.before_execute(retained,foreign_queue.Get(),1,array),"different actual queue admitted");
    require(store.before_execute(retained,consumer.Get(),1,array),"before-execute admission");
    require(!store.before_execute(retained,consumer.Get(),1,array),"same recording admitted twice");
    consumer->ExecuteCommandLists(1,array);
    const HRESULT signalled=consumer->Signal(completed.Get(),1);
    check(signalled,"consumer signal");require(store.after_signal(retained,signalled,1),"returned signal not recorded");
    require(store.collect()==0 && destroyed==0,"CPU-active use collected");
    auto* left=images[0].Get();auto* right=images[1].Get();images={texture(device,32),texture(device,32)};
    require(images[0]->GetDesc().Width==32 && images[1]->GetDesc().Width==32 &&
        retained->textures[0]->GetDesc().Width==16 && retained->textures[1]->GetDesc().Width==16,
        "caller size replacement changed retained old generation");
    consumer_record.command.Reset();consumer_record.allocator.Reset();consumer.Reset();
    require(store.held(left) && store.held(right) && store.size()==1,"caller resize/release lost submitted textures");
    store.finish_cpu(retained);require(destroyed==0 && store.size()==1,"unfinished physical GPU use released");
    require(retained->textures[0]->GetDesc().Width==16 && retained->recording && retained->allocator && retained->xr_destination,
        "retained physical interfaces vanished");
    check(gate->Signal(1),"release test gate");wait(completed.Get(),1);
    verify_pixels(result.Get());require(store.collect()==1 && destroyed==1 && store.size()==0,"physical fence did not retire exact use");
    std::printf("PASS %s queue: two distinct pixel outputs, blocked fence, caller release/resize, retained interfaces, exact retirement\n",cross_queue?"different":"same");
}
void reset_cases(ID3D12Device* device){
    auto consumer=queue(device);auto completed=fence(device);Recording recording(device),foreign(device);unsigned destroyed{};use::Store store{};
    auto owned=std::make_unique<TrackedUse>();owned->destroyed=&destroyed;owned->textures={texture(device),texture(device)};
    owned->recording=recording.command;owned->allocator=recording.allocator;owned->queue=consumer;owned->fence=completed;owned->recorded=true;
    auto* retained=store.adopt(std::move(owned));
    const HRESULT failed_reset=recording.command->Reset(recording.allocator.Get(),nullptr);
    require(FAILED(failed_reset) && store.discard_after_reset(recording.command.Get(),failed_reset)==0,
        "failed actual Reset discarded recording");
    store.finish_cpu(retained);require(store.size()==1 && destroyed==0,"CPU return freed recorded interfaces");
    foreign.close();const HRESULT foreign_reset=foreign.command->Reset(foreign.allocator.Get(),nullptr);
    check(foreign_reset,"foreign reset");require(store.discard_after_reset(foreign.command.Get(),foreign_reset)==0,"foreign Reset discarded actual recording");
    recording.close();const HRESULT failed_close=recording.command->Close();require(FAILED(failed_close),"actual repeated Close unexpectedly succeeded");
    require(store.size()==1 && destroyed==0,"Close failure released weakly recorded references");
    const HRESULT reset=recording.command->Reset(recording.allocator.Get(),nullptr);check(reset,"successful own Reset");
    require(store.discard_after_reset(recording.command.Get(),reset)==1 && destroyed==1,"successful own Reset did not discard unsubmitted use");
    unsigned unrecorded_destroyed{};auto unrecorded=std::make_unique<TrackedUse>();unrecorded->destroyed=&unrecorded_destroyed;
    auto* never_recorded=store.adopt(std::move(unrecorded));store.finish_cpu(never_recorded);
    require(unrecorded_destroyed==1 && store.size()==0,"unrecorded acquisition leaked");
    std::puts("PASS actual failed/successful Reset and Close; only own discarded recording releases interfaces");
}
void failure_case(ID3D12Device* device){
    auto consumer=queue(device);auto completed=fence(device);Recording recording(device);auto upload=upload_pixels(device);
    auto result=buffer(device,D3D12_HEAP_TYPE_READBACK,stride*2,D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<ComPtr<ID3D12Resource>,2> images{texture(device),texture(device)};
    populate(recording,upload.Get(),images);readback(recording,result.Get(),images);recording.close();
    use::Store store{};unsigned destroyed{};auto owned=std::make_unique<TrackedUse>();owned->destroyed=&destroyed;
    owned->textures=images;owned->recording=recording.command;owned->allocator=recording.allocator;owned->queue=consumer;owned->fence=completed;owned->recorded=true;
    auto* retained=store.adopt(std::move(owned));ID3D12CommandList* array[]{recording.command.Get()};
    require(store.before_execute(retained,consumer.Get(),1,array),"failure-case execution admission");consumer->ExecuteCommandLists(1,array);
    // Explicit simulated HRESULT seam. GPU Execute, actual later signal,
    // readback and all owned interfaces are physical. Do not induce device loss.
    require(!store.after_signal(retained,E_FAIL,1),"simulated failed Signal published Pending");
    check(consumer->Signal(completed.Get(),2),"independent physical completion signal");wait(completed.Get(),2);
    store.finish_cpu(retained);require(store.collect()==0 && destroyed==0,"unproven signal use retired from unrelated completion");
    verify_pixels(result.Get());
    // Module intentionally quarantines this use without an accepted signal.
    // The test has separately proven device idle and uses a NEW real signal.
    const HRESULT accepted=consumer->Signal(completed.Get(),3);check(accepted,"probe-only retry actual signal");
    require(store.after_signal(retained,accepted,3),"real returned signal cannot end test quarantine");wait(completed.Get(),3);
    require(store.collect()==1 && destroyed==1,"new exact signal did not release quarantine");
    std::puts("PASS simulated Signal failure retains physical use; unrelated completion cannot retire it; new actual signal/readback closes probe");
}
template<std::size_t N>
void replacement_case(ID3D12Device* device){
    namespace sets=w3vr::modern_eye_resource_set;
    std::array<ID3D12Resource*,N> active{};
    for(auto*& resource:active)resource=texture(device).Detach();
    const auto previous=active;
    auto consumer=queue(device);auto completed=fence(device),gate=fence(device);
    Recording recording(device);auto upload=upload_pixels(device);
    auto result=buffer(device,D3D12_HEAP_TYPE_READBACK,stride*2,D3D12_RESOURCE_STATE_COPY_DEST);
    {std::array<ComPtr<ID3D12Resource>,2> images{active[0],active[1]};
        populate(recording,upload.Get(),images);readback(recording,result.Get(),images);}
    recording.close();check(consumer->Wait(gate.Get(),1),"replacement blocked queue wait");
    ID3D12CommandList* submitted[]{recording.command.Get()};consumer->ExecuteCommandLists(1,submitted);
    check(consumer->Signal(completed.Get(),1),"replacement real signal");
    use::Store store{};std::array<sets::Slot,N> slots{};
    for(std::size_t i=0;i<N;++i)slots[i]={&active[i],completed.Get(),1,false};
    unsigned creates{};
    auto create=[&](std::size_t,ID3D12Resource** out)->HRESULT{++creates;*out=texture(device,32).Detach();return S_OK;};
    require(!sets::replace(slots,store,true,create) && creates==0 && active==previous,
        "unfinished real fence released/replaced old resource set");
    check(gate->Signal(1),"release replacement test gate");wait(completed.Get(),1);verify_pixels(result.Get());
    unsigned destroyed{};auto owned=std::make_unique<TrackedUse>();owned->destroyed=&destroyed;
    owned->textures={active[0],active[1]};auto* cpu_use=store.adopt(std::move(owned));
    require(!sets::replace(slots,store,true,create) && creates==0 && active==previous,
        "CPU-held source set replaced even after GPU completion");
    store.finish_cpu(cpu_use);require(destroyed==1,"unused CPU-only reference not released");
    slots[N-1].recording_pending=true;
    require(!sets::replace(slots,store,true,create) && creates==0,"pending original recording replaced");
    slots[N-1].recording_pending=false;
    slots[0].value=UINT64_MAX;
    require(!sets::replace(slots,store,true,create) && creates==0,"unknown target signal replaced");
    slots[0].value=1;slots[0].fence=nullptr;
    require(!sets::replace(slots,store,true,create) && creates==0,"missing original fence replaced");
    slots[0].fence=completed.Get();
    auto changed=slots;changed[1].owner=changed[0].owner;
    require(!sets::replace(changed,store,true,create) && creates==0,"aliased owning address admitted");
    changed=slots;changed[0].owner=nullptr;
    require(!sets::replace(changed,store,true,create) && creates==0,"null owning address admitted");
    for(std::size_t failure=0;failure<N;++failure){
        creates=0;
        auto partial=[&](std::size_t i,ID3D12Resource** out)->HRESULT{
            if(i==failure)return E_OUTOFMEMORY; // Explicit simulated allocation failure.
            return create(i,out);
        };
        require(!sets::replace(slots,store,true,partial),"partial allocation committed");
        require(creates==failure && active==previous,"partial allocation altered old owning set");
        for(auto* resource:active)require(resource->GetDesc().Width==16,"partial allocation invalidated old extent");
    }
    require(!sets::replace(slots,store,true,[](std::size_t,ID3D12Resource**)->HRESULT{return S_OK;}) && active==previous,
        "success-without-resource accepted");
    require(!sets::replace(slots,store,true,[&](std::size_t,ID3D12Resource** out)->HRESULT{
        *out=active[0];(*out)->AddRef();return S_OK;}) && active==previous,"old alias used as a new generation");
    ID3D12Resource* staged_first{};
    require(!sets::replace(slots,store,true,[&](std::size_t i,ID3D12Resource** out)->HRESULT{
        if(i==0){*out=texture(device,32).Detach();staged_first=*out;}
        else{*out=staged_first;(*out)->AddRef();}return S_OK;
    }) && active==previous,"aliased staging eyes accepted");
    unsigned late_destroyed{};use::Use* late_use{};
    require(!sets::replace(slots,store,true,[&](std::size_t i,ID3D12Resource** out)->HRESULT{
        if(i==N-1){auto late=std::make_unique<TrackedUse>();late->destroyed=&late_destroyed;
            late->textures[0]=active[0];late_use=store.adopt(std::move(late));}
        return create(i,out);
    }) && active==previous,"factory acquired old source but replacement skipped final check");
    store.finish_cpu(late_use);require(late_destroyed==1,"late CPU use not finished");
    ComPtr<ID3D12Resource> old_first=active[0];ID3D12Resource* externally_changed{};
    require(!sets::replace(slots,store,true,[&](std::size_t i,ID3D12Resource** out)->HRESULT{
        if(i==N-1){active[0]->Release();active[0]=texture(device,64).Detach();externally_changed=active[0];}
        return create(i,out);
    }) && active[0]==externally_changed && active[0]->GetDesc().Width==64,
        "factory changed owning address but stale snapshot overwrote it");
    active[0]->Release();active[0]=old_first.Detach();require(active==previous,"test restoration mismatch");
    // Reuse the actual old textures after all failed transactions; GetDesc
    // alone would not prove that they still supply the expected pixel bytes.
    Recording reuse_record(device);
    {std::array<ComPtr<ID3D12Resource>,2> images{active[0],active[1]};
        readback(reuse_record,result.Get(),images);}
    reuse_record.close();ID3D12CommandList* reuse_lists[]{reuse_record.command.Get()};
    consumer->ExecuteCommandLists(1,reuse_lists);check(consumer->Signal(completed.Get(),2),"reuse old set signal");
    wait(completed.Get(),2);verify_pixels(result.Get());
    creates=0;require(sets::replace(slots,store,true,create) && creates==N,"completed full replacement rejected");
    for(std::size_t i=0;i<N;++i){
        require(active[i]!=previous[i] && active[i]->GetDesc().Width==32,"complete replacement lost new texture");
        for(std::size_t j=0;j<i;++j)require(active[i]!=active[j],"new outputs are aliased");
    }
    for(auto*& resource:active){resource->Release();resource=nullptr;}
    std::printf("PASS %zu-resource host replacement: physical pending fence/readback, held references, original recording, every simulated create failure, atomic 16->32 commit\n",N);
}
void binding_case(ID3D12Device* device,IDXGIFactory6* factory){
    namespace binding=w3vr::modern_graphics_binding;
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"W3VRBindingProbeHidden";
    require(RegisterClassW(&wc)!=0,"own hidden window class");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"W3VR binding probe",WS_POPUP,0,0,64,64,
        nullptr,nullptr,instance,nullptr);require(window!=nullptr,"own hidden window");
    require(!IsWindowVisible(window),"test window became visible");
    {
        auto original_queue=queue(device);
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=32;desc.Height=32;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;
        const HRESULT created=factory->CreateSwapChainForHwnd(original_queue.Get(),window,&desc,nullptr,nullptr,&chain);
        check(created,"actual hidden native swapchain factory");
        binding::Registry registry;
        require(!registry.ready() && !registry.acquire_backbuffer().resource,"empty binding adopted a buffer");
        require(!registry.publish_factory(E_FAIL,original_queue.Get(),chain.Get()) && !registry.ready(),
            "explicit failed factory HRESULT published binding");
        require(registry.publish_factory(created,original_queue.Get(),chain.Get()),"actual factory queue/output binding rejected");
        auto first=registry.acquire();require(first && registry.current(first) && first->width==32 && first->height==32,
            "factory snapshot is not exact current owner");
        require(registry.publish_factory(created,original_queue.Get(),chain.Get()) && registry.acquire().get()==first.get(),
            "same callback advanced epoch or replaced owner");
        auto copied=std::make_shared<binding::Data>(*first);
        require(!registry.current(copied),"copied snapshot acquired registry authority");
        D3D12_COMMAND_QUEUE_DESC copy_desc{};copy_desc.Type=D3D12_COMMAND_LIST_TYPE_COPY;
        ComPtr<ID3D12CommandQueue> wrong_queue;check(device->CreateCommandQueue(&copy_desc,IID_PPV_ARGS(&wrong_queue)),"actual copy queue");
        require(!registry.publish_factory(S_OK,wrong_queue.Get(),chain.Get()) && registry.current(first),
            "non-DIRECT original queue published binding");
        auto auxiliary_queue=queue(device);ComPtr<IDXGISwapChain1> auxiliary;
        const HRESULT auxiliary_created=factory->CreateSwapChainForComposition(auxiliary_queue.Get(),&desc,nullptr,&auxiliary);
        check(auxiliary_created,"actual auxiliary composition swapchain factory");
        require(!registry.publish_factory(auxiliary_created,auxiliary_queue.Get(),auxiliary.Get()) && registry.current(first),
            "auxiliary real factory silently retargeted game binding");
        require(!registry.publish_factory(created,auxiliary_queue.Get(),chain.Get()) && registry.current(first),
            "same device alone inferred a different factory queue");
        unsigned auxiliary_calls{};
        const HRESULT auxiliary_resize=registry.resize_from_callback(auxiliary.Get(),[&]()->HRESULT{
            ++auxiliary_calls;return auxiliary->ResizeBuffers(2,48,48,DXGI_FORMAT_R8G8B8A8_UNORM,0);
        });check(auxiliary_resize,"actual auxiliary resize");
        require(auxiliary_calls==1 && registry.current(first) && registry.acquire()->width==32,
            "foreign resize invalidated main binding or repeated Original");
        auto held=registry.acquire_backbuffer();require(held.resource && registry.current(held.binding) &&
            held.resource->GetDesc().Width==32,"owned current backbuffer query");
        Recording own_record(device);require(registry.owns_child(first,own_record.command.Get()),"actual device child mismatch");own_record.close();
        unsigned original_calls{},successful{};
        const HRESULT failed=registry.resize_from_callback(chain.Get(),[&]()->HRESULT{
            ++original_calls;require(!registry.ready() && !registry.acquire_backbuffer().resource,
                "resize allowed acquisition while Original was active");
            return chain->ResizeBuffers(2,64,64,DXGI_FORMAT_R8G8B8A8_UNORM,0);
        });
        require(FAILED(failed) && original_calls==1,"held real backbuffer unexpectedly resized or Original called twice");
        auto after_failure=registry.acquire();require(after_failure && registry.current(after_failure) &&
            after_failure->epoch>first->epoch && after_failure->width==32 && !registry.current(first),
            "failed Original resize changed dimensions or retained old authority");
        held.resource.Reset();
        const HRESULT resized=registry.resize_from_callback(chain.Get(),[&]()->HRESULT{
            ++original_calls;return chain->ResizeBuffers(2,64,64,DXGI_FORMAT_R8G8B8A8_UNORM,0);
        });check(resized,"actual successful resize");++successful;
        auto after_success=registry.acquire();require(after_success && after_success->width==64 && after_success->height==64 &&
            after_success->epoch>after_failure->epoch && !registry.current(after_failure),"successful resize did not publish exact epoch/extent");
        auto new_source=registry.acquire_backbuffer();require(new_source.resource && new_source.resource->GetDesc().Width==64,
            "resized native backbuffer extent mismatch");new_source.resource.Reset();
        // The test owns this window and never shows it. No desktop automation,
        // game, headset, Native render hook or OpenXR is involved.
        std::barrier start{4};std::atomic<bool> reader_failed{};std::atomic<unsigned> observed{};
        std::array<std::thread,3> readers;
        for(auto& reader:readers)reader=std::thread([&]{
            auto initial=registry.acquire();if(!initial)reader_failed.store(true);
            start.arrive_and_wait();
            for(unsigned n=0;n<300;++n){auto snapshot=registry.acquire();
                if(!snapshot)continue;
                if(!snapshot->device || !snapshot->queue || !snapshot->chain ||
                    snapshot->queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT || !binding::finite(snapshot->epoch))
                    reader_failed.store(true);
                observed.fetch_add(1);
            }
        });
        start.arrive_and_wait();
        for(unsigned n=0;n<8;++n){const UINT extent=64+(n%2)*16;
            const HRESULT changed=registry.resize_from_callback(chain.Get(),[&]()->HRESULT{
                ++original_calls;return chain->ResizeBuffers(2,extent,extent,DXGI_FORMAT_R8G8B8A8_UNORM,0);
            });check(changed,"concurrent snapshot actual resize");++successful;
        }
        for(auto& reader:readers)reader.join();
        require(!reader_failed.load(),"concurrent owned snapshot lost physical interfaces");
        // Even when all loop reads meet a revoked phase, their initial owned
        // snapshots remain valid. Do not infer a minimum scheduling interleave.
        original_queue.Reset();chain.Reset();
        require(first->queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT && first->chain,
            "caller released factory interfaces but snapshot lost ownership");
        registry.stop_no_release();require(!registry.ready() && !registry.acquire() && !registry.current(after_success),
            "atomic stop retained new acquisition authority");
        require(first->queue->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,
            "registry revocation invalidated an existing owned reference");
        std::printf("PASS owned graphics binding: actual factory/GetBuffer, 1 actual failed + %u successful ResizeBuffers, %u Original calls, 3 reader threads (%u snapshots), auxiliary chain isolated, epoch rejection and retained interfaces\n",
            successful,original_calls,observed.load());
    }
    require(DestroyWindow(window)!=0,"destroy own hidden window");
    require(UnregisterClassW(wc.lpszClassName,instance)!=0,"unregister own hidden class");
}
struct TrackedSource:w3vr::modern_backbuffer_source::Record {
    unsigned* destroyed{};
    ~TrackedSource(){++*destroyed;}
};
void resize1_case(ID3D12Device* device,IDXGIFactory6* factory){
    namespace binding=w3vr::modern_graphics_binding;
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;
    wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"W3VRResize1ProbeHidden";
    require(RegisterClassW(&wc)!=0,"resize1 class");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"W3VR Resize1 probe",WS_POPUP,0,0,32,32,nullptr,nullptr,instance,nullptr);
    require(window && !IsWindowVisible(window),"resize1 hidden window");
    {
        auto first_queue=queue(device),second_queue=queue(device);
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=16;desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain1;check(factory->CreateSwapChainForHwnd(first_queue.Get(),window,&desc,nullptr,nullptr,&chain1),"resize1 factory");
        ComPtr<IDXGISwapChain3> chain;check(chain1.As(&chain),"resize1 interface");binding::Registry registry;
        require(registry.publish_factory(S_OK,first_queue.Get(),chain.Get()),"resize1 initial binding");
        auto initial=registry.acquire();auto held=registry.acquire_backbuffer();
        require(initial->present_count==2 && held.present_queue.Get()==first_queue.Get(),"factory queue per-buffer binding");
        const UINT masks[]{1,1};IUnknown* explicit_queues[]{second_queue.Get(),first_queue.Get()};unsigned originals{};
        auto resize=[&](UINT count,UINT extent,const UINT* actual_masks,IUnknown* const* actual_queues){
            return registry.resize1_from_callback(chain.Get(),count,actual_masks,actual_queues,[&]{
                ++originals;require(!registry.acquire() && !registry.current(initial),"resize1 admitted capture during Original");
                return chain->ResizeBuffers1(count,extent,extent,DXGI_FORMAT_R8G8B8A8_UNORM,0,actual_masks,actual_queues);
            });
        };
        const HRESULT failure=resize(2,32,masks,explicit_queues);
        require(FAILED(failure) && originals==1,"held source did not block actual ResizeBuffers1 or Original repeated");
        auto failed=registry.acquire();require(failed && failed->epoch>initial->epoch && failed->width==16 &&
            failed->queue.Get()==first_queue.Get() && !failed->explicit_present_queues,"failed Resize1 published new input queue");
        held.resource.Reset();check(resize(2,16,masks,explicit_queues),"actual Resize1 remap");
        auto mapped=registry.acquire();require(mapped && mapped->explicit_present_queues && mapped->present_count==2 &&
            mapped->present_queues[0].Get()==second_queue.Get() && mapped->present_queues[1].Get()==first_queue.Get() &&
            mapped->creation_masks[0]==1 && mapped->epoch>failed->epoch && !registry.current(failed),"explicit per-buffer queue map lost");
        // Use the actual current buffer's declared queue, including the remap.
        auto current=registry.acquire_backbuffer();require(current.resource && current.index<2 &&
            current.present_queue.Get()==mapped->present_queues[current.index].Get(),"current index queue did not come from explicit array");
        auto upload=upload_pixels(device),result=buffer(device,D3D12_HEAP_TYPE_READBACK,stride*2,D3D12_RESOURCE_STATE_COPY_DEST);
        Recording writer(device);D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=current.resource.Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        writer.command->ResourceBarrier(1,&b);auto input=placed(upload.Get(),0),target=subresource(current.resource.Get());
        writer.command->CopyTextureRegion(&target,0,0,0,&input,nullptr);++copies;
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;writer.command->ResourceBarrier(1,&b);
        for(UINT i=0;i<2;++i){auto output=placed(result.Get(),i*stride);writer.command->CopyTextureRegion(&output,0,0,0,&target,nullptr);++copies;}
        b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;writer.command->ResourceBarrier(1,&b);writer.close();
        ID3D12CommandList* lists[]{writer.command.Get()};current.present_queue->ExecuteCommandLists(1,lists);
        auto finished=fence(device);check(current.present_queue->Signal(finished.Get(),1),"remapped actual signal");wait(finished.Get(),1);verify_pixels(result.Get(),0x31,0x31);
        current.resource.Reset();
        check(resize(0,16,masks,explicit_queues),"zero-count actual Resize1 same queue map");
        auto zero=registry.acquire();require(zero && zero->buffer_count==2 && zero->present_queues[0].Get()==second_queue.Get(),"zero-count resize lost unchanged map");
        ComPtr<IDXGISwapChain1> auxiliary1;check(factory->CreateSwapChainForComposition(first_queue.Get(),&desc,nullptr,&auxiliary1),"resize1 auxiliary factory");
        ComPtr<IDXGISwapChain3> auxiliary;check(auxiliary1.As(&auxiliary),"resize1 auxiliary interface");unsigned foreign_calls{};
        IUnknown* auxiliary_queues[]{first_queue.Get(),first_queue.Get()};
        check(registry.resize1_from_callback(auxiliary.Get(),2,masks,auxiliary_queues,[&]{++foreign_calls;
            return auxiliary->ResizeBuffers1(2,16,16,DXGI_FORMAT_R8G8B8A8_UNORM,0,masks,auxiliary_queues);}),"actual auxiliary Resize1");
        require(foreign_calls==1 && registry.current(zero),"auxiliary Resize1 changed main authority");
        check(registry.resize_from_callback(chain.Get(),[&]{++originals;return chain->ResizeBuffers(2,16,16,DXGI_FORMAT_R8G8B8A8_UNORM,0);}),"plain actual resize after explicit map");
        require(!registry.acquire() && !registry.acquire_backbuffer().resource,"plain resize guessed explicit queue association");
        check(resize(2,16,masks,explicit_queues),"explicit queue association recovery");
        require(registry.ready() && registry.acquire()->epoch>zero->epoch,"explicit actual remap did not recover acquisition");
        first_queue.Reset();second_queue.Reset();
        require(mapped->present_queues[0]->GetDesc().Type==D3D12_COMMAND_LIST_TYPE_DIRECT,"old snapshot lost remapped queue reference");
        std::printf("PASS actual ResizeBuffers1: per-buffer queue remap, native pixels, retained-source failure, zero-count same map, auxiliary isolation, plain-map rejection/recovery; Original calls=%u plus auxiliary=%u\n",originals,foreign_calls);
    }
    require(DestroyWindow(window)!=0 && UnregisterClassW(wc.lpszClassName,instance)!=0,"resize1 window cleanup");
}
namespace native_source_probe {
namespace source=w3vr::modern_backbuffer_source;
namespace hooks=w3vr::modern_backbuffer_hooks;
using Reset=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using Close=HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*);
using Execute=void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
using Barrier=void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,UINT,const D3D12_RESOURCE_BARRIER*);
Reset reset_original{};Close close_original{};Execute execute_original{};Barrier barrier_original{};
struct Context {
    source::Store* store{};std::mutex* mutex{};ID3D12GraphicsCommandList* command{};
    ID3D12Fence* fence{};source::Batch last_batch;unsigned executes{},closes{},resets{},barriers{};
    UINT expected_count{};ID3D12CommandList* const* expected_array{};bool args_preserved{true},original_unlocked{true};
};
thread_local Context* active{};
HRESULT STDMETHODCALLTYPE close_hook(ID3D12GraphicsCommandList* command){
    const HRESULT result=close_original(command);
    if(active && active->command==command){++active->closes;std::lock_guard lock{*active->mutex};active->store->after_close(command,8,result);}
    return result;
}
HRESULT STDMETHODCALLTYPE reset_hook(ID3D12GraphicsCommandList* command,ID3D12CommandAllocator* allocator,ID3D12PipelineState* state){
    const HRESULT result=reset_original(command,allocator,state);
    if(active && active->command==command){++active->resets;std::lock_guard lock{*active->mutex};active->store->after_reset(command,8,result);}
    return result;
}
void STDMETHODCALLTYPE barrier_hook(ID3D12GraphicsCommandList* command,UINT count,const D3D12_RESOURCE_BARRIER* barriers){
    barrier_original(command,count,barriers);
    if(active && active->command==command)++active->barriers;
}
void STDMETHODCALLTYPE execute_hook(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists){
    auto* context=active;
    const bool ours=context && lists && std::find(lists,lists+count,context->command)!=lists+count;
    if(!ours){execute_original(queue,count,lists);return;}
    context->args_preserved=context->args_preserved && count==context->expected_count && lists==context->expected_array;
    context->last_batch=context->store->execute_from_callback(*context->mutex,queue,count,lists,
        [&](auto* command){return command==context->command?8ULL:0ULL;},[&]{
            ++context->executes;
            const bool unlocked=context->mutex->try_lock();context->original_unlocked=context->original_unlocked && unlocked;
            if(unlocked)context->mutex->unlock();execute_original(queue,count,lists);
        });
    const HRESULT signalled=queue->Signal(context->fence,1);
    {std::lock_guard lock{*context->mutex};context->store->after_signal(context->last_batch,SUCCEEDED(signalled),context->fence,1);}
}
void* method(IUnknown* object,std::size_t slot){return (*reinterpret_cast<void***>(object))[slot];}
}
void source_lifetime_case(ID3D12Device* device,IDXGIFactory6* factory,unsigned mode,bool native_callbacks=false){

    namespace source=w3vr::modern_backbuffer_source;
    namespace graphics=w3vr::modern_graphics_binding;
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"W3VRSourceProbeHidden";
    require(RegisterClassW(&wc)!=0,"source hidden class");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"W3VR source probe",WS_POPUP,0,0,32,32,nullptr,nullptr,instance,nullptr);
    require(window && !IsWindowVisible(window),"source hidden window");
    {
        auto producer=queue(device);auto completed=fence(device),gate=fence(device);
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=16;desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;
        desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;ComPtr<IDXGISwapChain1> chain;
        const HRESULT created=factory->CreateSwapChainForHwnd(producer.Get(),window,&desc,nullptr,nullptr,&chain);
        check(created,"source real factory");graphics::Registry binding;
        require(binding.publish_factory(created,producer.Get(),chain.Get()),"source binding publish");
        auto acquired=binding.acquire_backbuffer();require(acquired.resource!=nullptr,"source real GetBuffer");
        auto snapshot=acquired.binding;auto* native_source=acquired.resource.Get();
        std::array<ComPtr<ID3D12Resource>,2> destinations{texture(device),texture(device)};
        auto upload=upload_pixels(device),result=buffer(device,D3D12_HEAP_TYPE_READBACK,stride*2,D3D12_RESOURCE_STATE_COPY_DEST);
        Recording recording(device),empty(device);empty.close();
        source::Store retained{};std::mutex metadata_mutex;unsigned destroyed{};
        native_source_probe::Context observed{&retained,&metadata_mutex,recording.command.Get(),completed.Get()};
        if(native_callbacks)native_source_probe::active=&observed;
        for(unsigned eye=0;eye<2;++eye){
            auto r=std::make_unique<TrackedSource>();r->destroyed=&destroyed;r->binding=snapshot;
            r->source=acquired.resource;r->destination=destinations[eye];r->command=recording.command;
            r->key={{&w3vr::render_core::remastered_1048522,0x1000,
                {static_cast<std::uintptr_t>(0x2000+eye*0x100),9,100,static_cast<int>(eye),true,true}},8,50+eye,eye};
            auto* registered=retained.adopt(std::move(r));require(registered!=nullptr,"source-only adoption");
            retained.mark_recorded(registered);
            D3D12_RESOURCE_BARRIER to_write{};to_write.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            to_write.Transition.pResource=native_source;
            to_write.Transition.StateBefore=eye?D3D12_RESOURCE_STATE_COPY_SOURCE:D3D12_RESOURCE_STATE_PRESENT;
            to_write.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
            to_write.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            recording.command->ResourceBarrier(1,&to_write);
            auto pixels=placed(upload.Get(),eye*stride),target=subresource(native_source);
            recording.command->CopyTextureRegion(&target,0,0,0,&pixels,nullptr);++copies;
            std::swap(to_write.Transition.StateBefore,to_write.Transition.StateAfter);recording.command->ResourceBarrier(1,&to_write);
            // After each write restore COPY_SOURCE for the corresponding copy.
            if(eye==0){to_write.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
                to_write.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;recording.command->ResourceBarrier(1,&to_write);}
            recording.command->CopyResource(destinations[eye].Get(),native_source);++copies;
            retained.finish_cpu(registered);
        }
        D3D12_RESOURCE_BARRIER present{};present.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        present.Transition.pResource=native_source;present.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
        present.Transition.StateAfter=D3D12_RESOURCE_STATE_PRESENT;present.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        recording.command->ResourceBarrier(1,&present);
        for(const auto& dst:destinations){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource=dst.Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            recording.command->ResourceBarrier(1,&b);}
        readback(recording,result.Get(),destinations);
        for(const auto& dst:destinations){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource=dst.Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            recording.command->ResourceBarrier(1,&b);}
        const HRESULT failed_reset=recording.command->Reset(recording.allocator.Get(),nullptr);
        require(FAILED(failed_reset),"actual open source list Reset unexpectedly succeeded");
        retained.after_reset(recording.command.Get(),8,failed_reset);
        require(retained.size()==2 && retained.collect()==0,"failed Reset discarded source recording");
        const HRESULT foreign_reset=empty.command->Reset(empty.allocator.Get(),nullptr);check(foreign_reset,"actual unrelated Reset");
        retained.after_reset(empty.command.Get(),8,foreign_reset);
        require(retained.size()==2 && retained.collect()==0,"unrelated Reset discarded source recording");
        retained.after_close(empty.command.Get(),8,empty.close());
        for(auto* record=retained.head;record;record=record->next)require(!record->closed,"foreign Close marked source recording closed");
        const HRESULT closed=recording.close();
        if(!native_callbacks){
            retained.after_close(recording.command.Get(),9,closed);
            retained.after_close(recording.command.Get(),0,closed);
            for(auto* record=retained.head;record;record=record->next)require(!record->closed,"wrong/unknown epoch inherited successful Close");
        }
        retained.after_close(recording.command.Get(),8,closed);acquired.resource.Reset();destinations={};
        require(retained.size()==2 && destroyed==0 && retained.held(native_source),"CPU return released recorded native buffer");
        unsigned resize_calls{};
        auto resize=[&]{return binding.resize_from_callback(chain.Get(),[&]()->HRESULT{
            ++resize_calls;return chain->ResizeBuffers(2,32,32,DXGI_FORMAT_R8G8B8A8_UNORM,0);});};
        require(FAILED(resize()),"recorded source was not retained across real resize");
        unsigned original_calls{};
        auto epoch=[&](auto* command){return command==recording.command.Get()?8ULL:0ULL;};
        ID3D12CommandList* lists[]{empty.command.Get(),recording.command.Get()};
        check(producer->Wait(gate.Get(),1),"block real source producer");
        source::Batch batch;
        if(native_callbacks){
            observed.expected_count=2;observed.expected_array=lists;producer->ExecuteCommandLists(2,lists);
            batch=observed.last_batch;original_calls=observed.executes;
        }else batch=retained.execute_from_callback(metadata_mutex,producer.Get(),2,lists,epoch,[&]{
            ++original_calls;producer->ExecuteCommandLists(2,lists);});
        require(batch.size()==2 && original_calls==1,"two-list Original array was suppressed or source missing");
        if(mode==2){
            retained.after_signal(batch,false,nullptr,0); // Explicit simulated signal failure.
            check(producer->Signal(completed.Get(),1),"independent physical source completion");
        }else{const HRESULT h=producer->Signal(completed.Get(),1);check(h,"source exact signal");
            retained.after_signal(batch,SUCCEEDED(h),completed.Get(),1);}
        require(retained.collect()==0 && FAILED(resize()),"pending source completed or resized prematurely");
        check(gate->Signal(1),"release real source producer gate");wait(completed.Get(),1);verify_pixels(result.Get());
        require(retained.collect()==0,"replayable recording released after only first completion");
        if(mode==1){
            auto second_queue=producer;auto second_done=fence(device),second_gate=fence(device);
            check(second_queue->Wait(completed.Get(),1),"explicit prior producer GPU dependency");
            check(second_queue->Wait(second_gate.Get(),1),"block second real producer");
            void* fresh_pixels{};D3D12_RANGE no_read{0,0};check(upload->Map(0,&no_read,&fresh_pixels),"fresh replay upload map");
            for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<16;++y)
                std::memset(static_cast<unsigned char*>(fresh_pixels)+eye*stride+y*256,eye?0xB3:0x5A,64);
            upload->Unmap(0,nullptr);
            ID3D12CommandList* replay_lists[]{recording.command.Get(),empty.command.Get()};
            auto replay=retained.execute_from_callback(metadata_mutex,second_queue.Get(),2,replay_lists,epoch,[&]{
                ++original_calls;second_queue->ExecuteCommandLists(2,replay_lists);copies+=6;});
            require(replay.size()==2,"repeated recording lost second physical queue use");
            const HRESULT h=second_queue->Signal(second_done.Get(),9);check(h,"second real source signal");
            retained.after_signal(replay,SUCCEEDED(h),second_done.Get(),9);
            Recording next_allocator(device);next_allocator.close();
            const HRESULT reset=recording.command->Reset(next_allocator.allocator.Get(),nullptr);check(reset,"actual source Reset while submitted");
            retained.after_reset(recording.command.Get(),8,reset);
            require(retained.collect()==0 && FAILED(resize()),"first completion retired second pending producer");
            check(second_gate->Signal(1),"release second source gate");wait(second_done.Get(),9);verify_pixels(result.Get(),0x5A,0xB3);
        }else{
            const HRESULT reset=recording.command->Reset(recording.allocator.Get(),nullptr);check(reset,"actual source successful Reset");
            retained.after_reset(recording.command.Get(),8,reset);
            if(mode==2){
                require(retained.collect()==0 && FAILED(resize()),"failed signal retired from unrelated completion");
                const HRESULT drain=producer->Signal(completed.Get(),2);check(drain,"probe-only actual source drain signal");
                retained.after_drain(producer.Get(),drain,completed.Get(),2);wait(completed.Get(),2);
            }
        }
        retained.collect();require(retained.size()==0 && destroyed==2,"source did not retire after seal and ALL exact signals");
        check(resize(),"real resize after source retirement");
        require(binding.acquire()->width==32,"source retirement resize dimensions");
        if(native_callbacks){
            require(observed.args_preserved && observed.original_unlocked && observed.executes==1 && observed.closes>0 && observed.resets>0 && observed.barriers>0,
                "actual Native callbacks did not preserve arguments/Original/unlocked call");
            native_source_probe::active=nullptr;
            std::printf("PASS real MinHook source callbacks: Execute=%u Close=%u Reset=%u Barrier=%u, original unlocked, exact 2-list array\n",
                observed.executes,observed.closes,observed.resets,observed.barriers);
        }
        std::printf("PASS native backbuffer source mode=%u: 2-list Original calls=%u, actual ResizeBuffers calls=%u, recorded/replay/submitted ownership, exact successful Reset and GPU readback\n",
            mode,original_calls,resize_calls);
    }
    require(DestroyWindow(window)!=0 && UnregisterClassW(wc.lpszClassName,instance)!=0,"close own source hidden window");
}
void read_only_source_case(ID3D12Device* device,IDXGIFactory6* factory){
    namespace source=w3vr::modern_backbuffer_source;
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;
    wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"W3VRReadSourceHidden";
    require(RegisterClassW(&wc)!=0,"read source class");
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"W3VR read source",WS_POPUP,0,0,32,32,nullptr,nullptr,instance,nullptr);
    require(window && !IsWindowVisible(window),"read source hidden window");
    {
        auto renderer=queue(device),reader=queue(device);auto rendered=fence(device),completed=fence(device),gate=fence(device);
        DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=16;desc.Height=16;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;const HRESULT created=factory->CreateSwapChainForHwnd(renderer.Get(),window,&desc,nullptr,nullptr,&chain);
        check(created,"read source actual factory");w3vr::modern_graphics_binding::Registry binding;
        require(binding.publish_factory(created,renderer.Get(),chain.Get()),"read source binding");auto snapshot=binding.acquire();
        std::array<ComPtr<ID3D12Resource>,2> inputs,destinations{texture(device),texture(device)};
        auto acquired=binding.acquire_backbuffer();require(acquired.resource!=nullptr,"current real read source");
        inputs={acquired.resource,acquired.resource};acquired.resource.Reset();
        Recording writer(device),recording(device),empty(device);empty.close();auto upload=upload_pixels(device);
        auto result=buffer(device,D3D12_HEAP_TYPE_READBACK,stride*2,D3D12_RESOURCE_STATE_COPY_DEST);
        for(UINT eye=0;eye<1;++eye){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource=inputs[eye].Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
            b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            writer.command->ResourceBarrier(1,&b);auto pixels=placed(upload.Get(),eye*stride),target=subresource(inputs[eye].Get());
            writer.command->CopyTextureRegion(&target,0,0,0,&pixels,nullptr);++copies;
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);writer.command->ResourceBarrier(1,&b);
        }
        writer.close();ID3D12CommandList* writer_lists[]{writer.command.Get()};renderer->ExecuteCommandLists(1,writer_lists);
        check(renderer->Signal(rendered.Get(),1),"real renderer signal");check(reader->Wait(rendered.Get(),1),"real cross-queue read dependency");
        source::Store retained{};std::mutex mutex;unsigned destroyed{};
        for(UINT eye=0;eye<2;++eye){auto r=std::make_unique<TrackedSource>();r->destroyed=&destroyed;
            r->binding=snapshot;r->source=inputs[eye];r->destination=destinations[eye];r->command=recording.command;
            r->key={{&w3vr::render_core::remastered_1048522,0x1000,{static_cast<std::uintptr_t>(0x2000+eye*0x100),9,101,static_cast<int>(eye),true,true}},8,60+eye,eye};
            auto* owned=retained.adopt(std::move(r));require(owned!=nullptr,"read-only source registration");retained.mark_recorded(owned);
            D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=inputs[eye].Get();
            b.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;recording.command->ResourceBarrier(1,&b);
            recording.command->CopyResource(destinations[eye].Get(),inputs[eye].Get());++copies;
            std::swap(b.Transition.StateBefore,b.Transition.StateAfter);recording.command->ResourceBarrier(1,&b);
            b.Transition.pResource=destinations[eye].Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;recording.command->ResourceBarrier(1,&b);
            retained.finish_cpu(owned);
        }
        readback(recording,result.Get(),destinations);retained.after_close(recording.command.Get(),8,recording.close());inputs={};destinations={};
        check(reader->Wait(gate.Get(),1),"block actual read-only source use");ID3D12CommandList* lists[]{empty.command.Get(),recording.command.Get()};unsigned originals{};
        auto batch=retained.execute_from_callback(mutex,reader.Get(),2,lists,[](auto*){return 8ULL;},[&]{++originals;reader->ExecuteCommandLists(2,lists);});
        const HRESULT signalled=reader->Signal(completed.Get(),1);check(signalled,"real reader signal");
        retained.after_signal(batch,SUCCEEDED(signalled),completed.Get(),1);
        Recording next_allocator(device);next_allocator.close();const HRESULT reset=recording.command->Reset(next_allocator.allocator.Get(),nullptr);
        check(reset,"read-only actual Reset");retained.after_reset(recording.command.Get(),8,reset);
        require(retained.size()==2 && destroyed==0,"read-only pending sources released");
        require(FAILED(chain->ResizeBuffers(2,32,32,DXGI_FORMAT_R8G8B8A8_UNORM,0)),"read-only pending native buffers resized");
        check(gate->Signal(1),"release read-only source gate");wait(completed.Get(),1);verify_pixels(result.Get(),0x31,0x31);retained.collect();
        require(retained.size()==0 && destroyed==2 && originals==1,"cross-queue source retirement or Original count wrong");
        check(chain->ResizeBuffers(2,32,32,DXGI_FORMAT_R8G8B8A8_UNORM,0),"resize after read-only exact completion");
        std::puts("PASS read-only native backbuffers on different DIRECT queue: actual writer queue preserved, real Wait, 2-list Original, exact signal/Reset and current-buffer pixels");
    }
    require(DestroyWindow(window)!=0 && UnregisterClassW(wc.lpszClassName,instance)!=0,"close read source hidden window");
}
void native_source_hooks_case(ID3D12Device* device,IDXGIFactory6* factory){
    namespace probe=native_source_probe;namespace hooks=probe::hooks;
    Recording anchor(device);auto native_queue=queue(device);
    hooks::Targets targets{probe::method(anchor.command.Get(),10),probe::method(native_queue.Get(),10),
        probe::method(anchor.command.Get(),9),probe::method(anchor.command.Get(),26)};
    hooks::Receipts receipts;
    require(!receipts.ready() && !receipts.accepts(targets),"empty hook receipts admitted Native capture");
    require(MH_Initialize()==MH_OK,"probe MinHook Initialize");
    const std::array<void*,4> detours{reinterpret_cast<void*>(&probe::reset_hook),reinterpret_cast<void*>(&probe::execute_hook),
        reinterpret_cast<void*>(&probe::close_hook),reinterpret_cast<void*>(&probe::barrier_hook)};
    const std::array<void**,4> originals{reinterpret_cast<void**>(&probe::reset_original),reinterpret_cast<void**>(&probe::execute_original),
        reinterpret_cast<void**>(&probe::close_original),reinterpret_cast<void**>(&probe::barrier_original)};
    for(std::size_t i=0;i<targets.size();++i){
        require(hooks::native_target(targets[i]),"probe target was not concrete Native DirectX");
        require(MH_CreateHook(targets[i],detours[i],originals[i])==MH_OK,"probe CreateHook");
        require(!receipts.accepts(targets),"unenabled/partial hooks admitted capture");
        require(MH_EnableHook(targets[i])==MH_OK,"probe EnableHook");
        require(receipts.publish(static_cast<hooks::Site>(i),targets[i],*originals[i],true),"real enabled hook receipt");
        if(i+1<targets.size())require(!receipts.accepts(targets),"partial Native bundle admitted capture");
    }
    require(receipts.ready() && receipts.accepts(targets),"complete exact Native bundle was rejected");
    auto mixed=targets;std::swap(mixed[0],mixed[2]);require(!receipts.accepts(mixed),"mixed Native methods admitted capture");
    mixed=targets;mixed[1]=nullptr;require(!receipts.accepts(mixed),"unknown Execute target admitted capture");
    hooks::Receipts failure;
    require(!failure.publish(hooks::Site::Reset,targets[0],*originals[0],false),"Create-only receipt counted as enabled");
    for(std::size_t i=0;i<targets.size();++i)failure.publish(static_cast<hooks::Site>(i),targets[i],*originals[i],true);
    require(!failure.accepts(targets),"failed receipt silently recovered through later enables");
    source_lifetime_case(device,factory,0,true);
    receipts.revoke();require(!receipts.ready() && !receipts.accepts(targets),"revoked receipt admitted capture");
    // Standalone probe has one calling thread and has drained all its actual
    // work. This does not claim the game's external all-call unload barrier.
    for(auto* target:targets){require(MH_DisableHook(target)==MH_OK,"probe disable after actual drain");
        require(MH_RemoveHook(target)==MH_OK,"probe remove with single probe thread stopped");}
    require(MH_Uninitialize()==MH_OK,"probe uninitialize");
    std::puts("PASS actual Native 4-hook receipts, partial/mixed/revoked rejection and GPU source lifecycle; game callbacks not tested");
}
int main(int argc,char** argv)try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter1> adapter;check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"hardware adapter");
    DXGI_ADAPTER_DESC1 description{};check(adapter->GetDesc1(&description),"adapter description");
    require((description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)==0,"physical GPU required");
    std::printf("GPU %ls dedicated_bytes=%llu\n",description.Description,static_cast<unsigned long long>(description.DedicatedVideoMemory));
    ComPtr<ID3D12Debug> debug;
    if(argc>1 && std::strcmp(argv[1],"--debug")==0 && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))){debug->EnableDebugLayer();std::puts("PROBE_DEBUG_LAYER=true");}
    ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    copy_case(device.Get(),false);copy_case(device.Get(),true);reset_cases(device.Get());failure_case(device.Get());replacement_case<4>(device.Get());replacement_case<6>(device.Get());binding_case(device.Get(),factory.Get());
    for(unsigned mode=0;mode<3;++mode)source_lifetime_case(device.Get(),factory.Get(),mode);
    read_only_source_case(device.Get(),factory.Get());
    native_source_hooks_case(device.Get(),factory.Get());
    resize1_case(device.Get(),factory.Get());
    std::printf("PASS shared host source-use store: %u checks, %u real texture copies, physical readback\n",checks,copies);
    std::puts("Signal/partial-allocation HRESULTs are simulated; real device removal not induced.");
    std::puts("GAME=false HEADSET=false OPENXR=false NATIVE_CAMERA=false VR_FUNCTIONAL=false");return 0;
}catch(const std::exception& error){std::printf("FAIL %s\n",error.what());return 1;}
