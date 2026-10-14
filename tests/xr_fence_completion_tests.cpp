#include "xr_fence_completion.h"
#include "openxr_image_protocol.h"
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
namespace f=w3vr::xr_fence;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value){std::cerr<<why<<'\n';std::exit(1);}}
void cpu_cases(){
    unsigned reads{},registrations{},waits{};
    auto read=[&]{++reads;return uint64_t(0);};
    auto reg=[&](uint64_t){++registrations;return true;};
    auto wait=[&]{++waits;return true;};
    check(f::wait_for_completion(0,read,reg,wait)&&reads==0&&registrations==0&&waits==0,"unused allocator");
    check(!f::wait_for_completion(UINT64_MAX,read,reg,wait)&&reads==0&&registrations==0&&waits==0,"unknown use retired");
    check(!f::completed(UINT64_MAX,5)&&!f::completed(9,0)&&!f::completed(9,UINT64_MAX),"removed or unknown treated completed");
    check(!f::completed(4,5)&&f::completed(5,5)&&f::completed(6,5),"exact finite completion");
    for(uint64_t first:{uint64_t(0),uint64_t(4),UINT64_MAX}){
        for(uint64_t second:{uint64_t(0),uint64_t(4),uint64_t(5),uint64_t(7),UINT64_MAX}){
            for(bool register_ok:{false,true})for(bool woke:{false,true}){
                reads=registrations=waits=0;
                auto query=[&]{return reads++==0?first:second;};
                auto registration=[&](uint64_t target){++registrations;check(target==5,"different registered target");return register_ok;};
                auto awaiting=[&]{++waits;return woke;};
                const bool expected=first!=UINT64_MAX&&register_ok&&woke&&second!=UINT64_MAX&&second>=5;
                check(f::wait_for_completion(5,query,registration,awaiting)==expected,"CPU failure/completion policy");
                check(reads==(first==UINT64_MAX||!register_ok||!woke?1u:2u),"unexpected extra read");
                check(registrations==(first==UINT64_MAX?0u:1u)&&waits==(first!=UINT64_MAX&&register_ok?1u:0u),"callback count");
            }
        }
    }
    reads=registrations=waits=0;
    check(f::wait_for_completion(5,[&]{++reads;return uint64_t(6);},reg,wait)&&
        reads==1&&registrations==0&&waits==0,"already completed registered stale event");
}
void physical_gpu(){
    wchar_t system[MAX_PATH]{};check(GetSystemDirectoryW(system,MAX_PATH)>0,"system directory");
    const auto path=std::wstring(system)+L"\\d3d12.dll";
    const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(module!=nullptr,"system D3D12 module");
    const auto create=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(module,"D3D12CreateDevice"));
    check(create!=nullptr,"D3D12 entry");
    // Module is retained for process lifetime, independently of the mod/SDK.
    ComPtr<ID3D12Device> device;check(SUCCEEDED(create(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"physical device");
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue,other;check(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"own queue");
    check(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&other)))&&queue.Get()!=other.Get(),"other queue");
    ComPtr<ID3D12Fence> exact,gate,unrelated;
    check(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&exact))),"exact fence");
    check(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))),"gate fence");
    check(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&unrelated))),"unrelated fence");
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr),other_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    check(event&&other_event,"own auto-reset events");
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    check(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"own allocator");
    check(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))),"own command list");
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=512;
    desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    ComPtr<ID3D12Resource> source,destination;
    check(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&source))),"own upload buffer");
    heap.Type=D3D12_HEAP_TYPE_READBACK;
    check(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&destination))),"own readback buffer");
    std::array<unsigned char,512> pattern{};for(unsigned n=0;n<pattern.size();++n)pattern[n]=static_cast<unsigned char>(n*37+11);
    void* mapped{};D3D12_RANGE none{0,0};check(SUCCEEDED(source->Map(0,&none,&mapped)),"map upload");
    memcpy(mapped,pattern.data(),pattern.size());source->Unmap(0,nullptr);
    list->CopyBufferRegion(destination.Get(),0,source.Get(),0,pattern.size());
    check(SUCCEEDED(list->Close()),"close own copy list");
    check(SUCCEEDED(queue->Wait(gate.Get(),1)),"real queue blocked before copy");
    ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
    check(SUCCEEDED(queue->Signal(exact.Get(),5)),"exact signal queued");
    check(exact->GetCompletedValue()<5,"gate failed to hold own copy");
    auto read=[&]{return exact->GetCompletedValue();};
    auto register_exact=[&](uint64_t value){return SUCCEEDED(exact->SetEventOnCompletion(value,event));};
    unsigned resets{};
    // This is the same admission helper used immediately before host Reset.
    // A real, deliberately unrelated Windows event wakeup must not admit it.
    const bool stale=f::wait_for_completion(5,read,register_exact,[&]{
        check(SetEvent(event)!=FALSE,"inject real stale event");return WaitForSingleObject(event,100)==WAIT_OBJECT_0;
    });
    if(stale){++resets;allocator->Reset();}
    check(!stale&&resets==0&&exact->GetCompletedValue()<5,"stale event admitted allocator reuse");
    check(SUCCEEDED(other->Signal(unrelated.Get(),900)),"other queue unrelated signal");
    check(f::wait_for_completion(900,[&]{return unrelated->GetCompletedValue();},
        [&](uint64_t value){return SUCCEEDED(unrelated->SetEventOnCompletion(value,other_event));},
        [&]{return WaitForSingleObject(other_event,5000)==WAIT_OBJECT_0;}),"other real fence completion");
    check(!f::wait_for_completion(5,read,register_exact,[&]{return WaitForSingleObject(event,20)==WAIT_OBJECT_0;})&&resets==0,"different queue fence retired exact allocator");
    check(SUCCEEDED(gate->Signal(1)),"release own GPU gate");
    check(f::wait_for_completion(5,read,register_exact,[&]{return WaitForSingleObject(event,5000)==WAIT_OBJECT_0;}),"exact GPU fence did not complete");
    ++resets;check(SUCCEEDED(allocator->Reset())&&resets==1,"allocator reset after exact completion");
    D3D12_RANGE bytes{0,pattern.size()};check(SUCCEEDED(destination->Map(0,&bytes,&mapped)),"map completed readback");
    for(unsigned n=0;n<pattern.size();++n)check(static_cast<unsigned char*>(mapped)[n]==pattern[n],"own copied byte mismatch");
    destination->Unmap(0,&none);
    check(CloseHandle(event)&&CloseHandle(other_event),"close events after exact completion");
}
void physical_image_helper(){
    // Own image layers + fake XR handle/frame admission. Runtime remains absent.
    wchar_t system[MAX_PATH]{};check(GetSystemDirectoryW(system,MAX_PATH)>0,"image system directory");
    const auto path=std::wstring(system)+L"\\d3d12.dll";
    const auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(module!=nullptr,"image system module");
    const auto create=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(module,"D3D12CreateDevice"));
    check(create!=nullptr,"image device entry");
    ComPtr<ID3D12Device> device;check(SUCCEEDED(create(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"image own device");
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;check(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"image queue");
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    check(SUCCEEDED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator))),"image allocator");
    check(SUCCEEDED(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&list))),"image list");
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=8;desc.Height=8;
    desc.DepthOrArraySize=2;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> source,destination,readback;
    check(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&source))),"image own RT array");
    check(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&destination))),"image own committed copy array");
    ComPtr<IUnknown> source_id,destination_id,source_alias_id;
    ComPtr<ID3D12Resource> source_alias;
    check(SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&source_id)))&&SUCCEEDED(destination->QueryInterface(IID_PPV_ARGS(&destination_id))),"canonical image identities");
    check(SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&source_alias)))&&SUCCEEDED(source_alias->QueryInterface(IID_PPV_ARGS(&source_alias_id)))&&source_alias_id.Get()==source_id.Get(),"same COM object identity");
    D3D12_DESCRIPTOR_HEAP_DESC h{};h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;h.NumDescriptors=2;
    ComPtr<ID3D12DescriptorHeap> rtv;check(SUCCEEDED(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&rtv))),"image RTV heap");
    auto handle=rtv->GetCPUDescriptorHandleForHeapStart();const auto increment=device->GetDescriptorHandleIncrementSize(h.Type);
    for(unsigned eye=0;eye<2;++eye){
        D3D12_RENDER_TARGET_VIEW_DESC view{};view.Format=desc.Format;view.ViewDimension=D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        view.Texture2DArray.FirstArraySlice=eye;view.Texture2DArray.ArraySize=1;device->CreateRenderTargetView(source.Get(),&view,handle);
        const float colors[2][4]{{1,0,0,1},{0,1,0,1}};list->ClearRenderTargetView(handle,colors[eye],0,nullptr);handle.ptr+=increment;
    }
    namespace x=w3vr::xr_image;x::Image image;x::Ticket ticket;
    const uintptr_t queue_key=reinterpret_cast<uintptr_t>(queue.Get());
    check(image.begin({{0x1000,1,1},0x2000,queue_key,3,1},ticket),"own helper fake XR begin");
    uint32_t index{};check(image.acquire(ticket,index,[](uint32_t& v){v=0;return XR_SUCCESS;})==XR_SUCCESS&&image.wait(ticket,[]{return XR_SUCCESS;})==XR_SUCCESS,"own helper fake acquire/wait");
    const x::Recording binding{reinterpret_cast<uintptr_t>(list.Get()),reinterpret_cast<uintptr_t>(source.Get()),reinterpret_cast<uintptr_t>(source_id.Get()),0};
    check(image.start_recording(ticket,binding,true),"bind real own list/resource to fake XR frame");
    unsigned before_count{},copy_count{},after_count{};
    auto barrier=[](ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){D3D12_RESOURCE_BARRIER v{};v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;v.Transition.pResource=r;v.Transition.StateBefore=a;v.Transition.StateAfter=b;v.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;return v;};
    const D3D12_RESOURCE_BARRIER before[]{barrier(source.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE),barrier(destination.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)};
    const D3D12_RESOURCE_BARRIER after[]{barrier(source.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET),barrier(destination.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};
    const auto record_before=[&]{++before_count;list->ResourceBarrier(2,before);};
    const auto copy=[&]{++copy_count;list->CopyResource(destination.Get(),source.Get());};
    const auto record_after=[&]{++after_count;list->ResourceBarrier(2,after);};
    check(!image.copy_source(ticket,binding,reinterpret_cast<uintptr_t>(source_alias_id.Get()),record_before,copy,record_after)&&before_count==0&&copy_count==0&&after_count==0,"real canonical alias ran copy");
    check(image.copy_source(ticket,binding,reinterpret_cast<uintptr_t>(destination_id.Get()),record_before,copy,record_after)&&before_count==1&&copy_count==1&&after_count==1&&image.color_ready(ticket,binding),"real own image helper sequence");
    const auto copied_to_source=barrier(destination.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE);list->ResourceBarrier(1,&copied_to_source);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2]{};UINT rows[2]{};UINT64 row_bytes[2]{},total{};
    device->GetCopyableFootprints(&desc,0,2,0,footprints,rows,row_bytes,&total);
    check(rows[0]==8&&rows[1]==8&&row_bytes[0]==32&&row_bytes[1]==32,"real image footprints");
    D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_READBACK;check(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"image readback");
    for(UINT eye=0;eye<2;++eye){D3D12_TEXTURE_COPY_LOCATION dst{},src{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprints[eye];src.pResource=destination.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=eye;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);}
    check(SUCCEEDED(list->Close())&&image.before_execute(ticket,binding,queue_key,true),"close/admit real own helper list");
    ComPtr<ID3D12Fence> fence,gate;check(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))&&SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))),"image fences");
    check(SUCCEEDED(queue->Wait(gate.Get(),1)),"image GPU gate");ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);
    check(image.execution_returned(ticket,binding,queue_key,true),"own image execute return");
    const HRESULT signal=queue->Signal(fence.Get(),1);check(SUCCEEDED(signal)&&image.signal(ticket,queue_key,true,1),"own image queue signal");
    check(fence->GetCompletedValue()<1&&image.releasable(ticket),"queue receipt confused with GPU complete");
    check(image.release(ticket,[]{return XR_SUCCESS;})==XR_SUCCESS&&image.layer_allowed(ticket),"fake runtime release receipt");
    const HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr&&SUCCEEDED(gate->Signal(1)),"open own image gate");
    check(f::wait_for_completion(1,[&]{return fence->GetCompletedValue();},[&](uint64_t value){return SUCCEEDED(fence->SetEventOnCompletion(value,event));},[&]{return WaitForSingleObject(event,5000)==WAIT_OBJECT_0;}),"own image exact completion");
    void* mapped{};D3D12_RANGE range{0,static_cast<SIZE_T>(total)};check(SUCCEEDED(readback->Map(0,&range,&mapped)),"map own two-layer image");
    const unsigned char expected[2][4]{{255,0,0,255},{0,255,0,255}};
    for(unsigned eye=0;eye<2;++eye)for(unsigned y=0;y<8;++y)for(unsigned px=0;px<8;++px)for(unsigned channel=0;channel<4;++channel){
        const auto offset=footprints[eye].Offset+y*footprints[eye].Footprint.RowPitch+px*4+channel;
        check(static_cast<const unsigned char*>(mapped)[offset]==expected[eye][channel],"own image layer pixel mismatch");
    }
    D3D12_RANGE none{0,0};readback->Unmap(0,&none);check(CloseHandle(event)!=FALSE,"image event drain");
}
int main(){cpu_cases();physical_gpu();physical_image_helper();std::cout<<checks<<" checks: real own DirectX queues, blocked GPU copy, stale Windows event, exact fence, allocator Reset and 512 copied bytes. Additional real own two-layer RT/COPY_SOURCE/RT helper copied and checked 512 image bytes; canonical COM alias rejected. XR handles/acquire/wait/release receipts, device-removed readings and failure outcomes are fixtures. No OpenXR runtime, host callbacks, SDK, Witcher or Quest executed.\n";}
