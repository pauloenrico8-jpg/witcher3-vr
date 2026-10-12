#include "modern_eye_source_use.h"
#include "modern_eye_resource_set.h"
#include "modern_graphics_binding.h"
#include <barrier>
#include <thread>
#include <dxgi1_6.h>
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
    void close(){check(command->Close(),"close");}
};
void wait(ID3D12Fence* finished,UINT64 value){
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);require(event!=nullptr,"event");
    const HRESULT result=finished->SetEventOnCompletion(value,event);
    if(FAILED(result)){CloseHandle(event);check(result,"completion event");}
    const DWORD waited=WaitForSingleObject(event,10000);CloseHandle(event);
    require(waited==WAIT_OBJECT_0,"GPU completion timeout");
    require(finished->GetCompletedValue()!=UINT64_MAX && finished->GetCompletedValue()>=value,"device loss/unfinished fence");
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
void verify_pixels(ID3D12Resource* result){
    void* bytes{};D3D12_RANGE range{0,stride*2};check(result->Map(0,&range,&bytes),"readback map");
    for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x)
        require(static_cast<unsigned char*>(bytes)[eye*stride+y*256+x]==(eye?0xC9:0x31),"distinct eye pixel copy mismatch");
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
int main()try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    ComPtr<IDXGIFactory6> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"factory");
    ComPtr<IDXGIAdapter1> adapter;check(factory->EnumAdapterByGpuPreference(0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)),"hardware adapter");
    DXGI_ADAPTER_DESC1 description{};check(adapter->GetDesc1(&description),"adapter description");
    require((description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)==0,"physical GPU required");
    std::printf("GPU %ls dedicated_bytes=%llu\n",description.Description,static_cast<unsigned long long>(description.DedicatedVideoMemory));
    ComPtr<ID3D12Device> device;check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"device");
    copy_case(device.Get(),false);copy_case(device.Get(),true);reset_cases(device.Get());failure_case(device.Get());replacement_case<4>(device.Get());replacement_case<6>(device.Get());binding_case(device.Get(),factory.Get());
    std::printf("PASS shared host source-use store: %u checks, %u real texture copies, physical readback\n",checks,copies);
    std::puts("Signal/partial-allocation HRESULTs are simulated; real device removal not induced.");
    std::puts("GAME=false HEADSET=false OPENXR=false NATIVE_CAMERA=false VR_FUNCTIONAL=false");return 0;
}catch(const std::exception& error){std::printf("FAIL %s\n",error.what());return 1;}
