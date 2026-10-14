#include "xr_fence_completion.h"
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
int main(){cpu_cases();physical_gpu();std::cout<<checks<<" checks: real own DirectX queues, blocked GPU copy, stale Windows event, exact fence, allocator Reset and 512 copied bytes. Device-removed readings and failure outcomes are CPU fixtures. No OpenXR runtime, host callbacks, SDK, Witcher or Quest executed.\n";}
