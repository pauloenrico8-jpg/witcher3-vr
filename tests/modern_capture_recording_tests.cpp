#include "xr_allocator_submissions.h"
#include "modern_capture_recording.h"
#include "modern_backbuffer_source.h"
#include <mutex>
#include <thread>
#include "modern_eye_source_use.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <array>
namespace a=w3vr::xr_allocator;
using Microsoft::WRL::ComPtr;
unsigned checks{},signal_calls{};bool fail_signal{};
void check(bool v,const char* why){++checks;if(!v){std::cerr<<why<<'\n';std::exit(1);}}
HRESULT signal_callback(ID3D12CommandQueue* q,ID3D12Fence* f,uint64_t n){++signal_calls;return fail_signal?E_FAIL:q->Signal(f,n);}
struct Fixture {
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue,other;
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> command;
    ComPtr<ID3D12Fence> shared,gate,drain;
    ComPtr<ID3D12Resource> upload,output;
    HANDLE event{};std::array<unsigned char,512> pattern{};
    Fixture(){
        wchar_t system[MAX_PATH]{};check(GetSystemDirectoryW(system,MAX_PATH)>0,"system directory");
        const auto path=std::wstring(system)+L"\\d3d12.dll";
        auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);check(module!=nullptr,"own system D3D12");
        auto create=reinterpret_cast<decltype(&D3D12CreateDevice)>(GetProcAddress(module,"D3D12CreateDevice"));check(create!=nullptr,"own create entry");
        check(SUCCEEDED(create(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"own physical device");
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)))&&SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&other))),"own distinct queues");
        check(SUCCEEDED(device->CreateCommandAllocator(q.Type,IID_PPV_ARGS(&allocator))),"own allocator");
        check(SUCCEEDED(device->CreateCommandList(0,q.Type,allocator.Get(),nullptr,IID_PPV_ARGS(&command))),"own list");
        check(SUCCEEDED(command->Close()),"initial close");
        check(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&shared)))&&
            SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)))&&
            SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&drain))),"own fences");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr);check(event!=nullptr,"own drain event");
        D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=512;r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES h{};h.Type=D3D12_HEAP_TYPE_UPLOAD;
        check(SUCCEEDED(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload))),"own upload");
        h.Type=D3D12_HEAP_TYPE_READBACK;
        check(SUCCEEDED(device->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&r,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&output))),"own output");
        for(unsigned n=0;n<pattern.size();++n)pattern[n]=static_cast<unsigned char>(n*19+7);
        void* data{};D3D12_RANGE none{0,0};check(SUCCEEDED(upload->Map(0,&none,&data)),"map upload");memcpy(data,pattern.data(),pattern.size());upload->Unmap(0,nullptr);
    }
    ~Fixture(){if(event)CloseHandle(event);}
    void reset(a::Store& store){check(SUCCEEDED(allocator->Reset()),"actual allocator reset");const auto hr=command->Reset(allocator.Get(),nullptr);check(hr==S_OK&&store.observe_reset(command.Get(),hr),"actual list reset observed");}
    a::Objects objects(unsigned nonce=1,uint64_t value=5){a::Objects o;o.frame={0x1000,1,nonce};o.generation=7;o.shared_value=value;o.device=device;o.queue=queue;o.shared_fence=shared;o.allocator=allocator;o.command=command;o.destination=output;return o;}
    void copy(){command->CopyBufferRegion(output.Get(),0,upload.Get(),0,pattern.size());check(SUCCEEDED(command->Close()),"close copy");}
    void unblock(){check(SUCCEEDED(gate->Signal(1)),"unblock own GPU");check(SUCCEEDED(queue->Signal(drain.Get(),1)),"own independent drain signal");
        check(w3vr::xr_fence::wait_for_completion(1,[&]{return drain->GetCompletedValue();},[&](uint64_t n){return drain->SetEventOnCompletion(n,event)==S_OK;},[&]{return WaitForSingleObject(event,5000)==WAIT_OBJECT_0;}),"own independent drain completion");}
    void pixels(ID3D12Resource* resource){void* data{};D3D12_RANGE bytes{0,pattern.size()};check(SUCCEEDED(resource->Map(0,&bytes,&data)),"completed map");
        for(unsigned n=0;n<pattern.size();++n)check(static_cast<unsigned char*>(data)[n]==pattern[n],"retained copy byte mismatch");D3D12_RANGE none{0,0};resource->Unmap(0,&none);}
};


namespace source=w3vr::modern_backbuffer_source;
unsigned destroyed{},gpu_copies{},allocation_attempts{};
struct Tracked:source::Record {~Tracked(){++destroyed;}};
template<class T> struct FailingAllocator {
    using value_type=T;FailingAllocator()=default;template<class U> FailingAllocator(const FailingAllocator<U>&){}
    T* allocate(std::size_t){++allocation_attempts;throw std::bad_alloc();}
    void deallocate(T*,std::size_t)noexcept{}bool operator==(const FailingAllocator&)const{return true;}
};
void recording_case(unsigned fault){
    Fixture f;source::Store sources;const auto destroyed_before=destroyed;
    auto data=std::make_shared<w3vr::modern_graphics_binding::Data>();data->epoch=1;data->device=f.device;
    data->device_identity=w3vr::modern_graphics_binding::identity(f.device.Get());data->queue=f.queue;
    check(SUCCEEDED(f.allocator->Reset())&&SUCCEEDED(f.command->Reset(f.allocator.Get(),nullptr)),"actual recording Reset");
    source::Record* retained{};bool internal{},commands_returned{},slot_reserved=true,blocked{},publication{};
    unsigned finishes{},failures{},records{},publishes{};const auto attempts_before=allocation_attempts;
    auto prepare=[&]{
        if(fault==0)return false;
        auto record=std::make_unique<Tracked>();record->binding=data;record->source=f.upload;record->destination=f.output;record->command=f.command;
        record->key.identity={&w3vr::render_core::remastered_1048522,0x1000,{0x2000,7,1,0,true,true,1000,{0x3000,1,1}}};
        record->key.recording_epoch=1;record->key.serial=1;record->key.slot=0;
        retained=sources.adopt(std::move(record));check(retained!=nullptr,"own record admission");
        if(fault==1)throw std::bad_alloc();return true;
    };
    auto record=[&]{
        ++records;check(internal,"capture flag not active during commands");sources.mark_recorded(retained);
        if(fault==2)throw std::bad_alloc();
        f.copy();
        if(fault==3)throw std::bad_alloc();
        commands_returned=true;
    };
    auto publish=[&]{
        ++publishes;check(!internal,"capture flag leaked into publication");
        if(fault==4){std::vector<int,FailingAllocator<int>> pending;pending.push_back(1);}
        if(fault==5)return false;
        publication=true;return true;
    };
    auto failed=[&](bool referenced)noexcept{
        ++failures;if(!referenced)slot_reserved=false;
        else{blocked=true;if(retained&&!commands_returned)retained->quarantined=true;}
        publication=false;
    };
    auto finish=[&]()noexcept{++finishes;if(retained)sources.finish_cpu(retained);};
    const bool result=w3vr::capture_recording::run(internal,prepare,record,publish,failed,finish);
    check(result==(fault==6)&&finishes==1&&!internal,"result/CPU finish/internal flag mismatch");
    check(failures==(fault==6?0u:1u)&&publication==(fault==6),"partial publication or duplicate failure");
    check(records==(fault>=2?1u:0u)&&publishes==(fault>=4?1u:0u),"preparation/recording failure replayed commands");
    if(fault<2){
        check(!slot_reserved&&!blocked&&sources.size()==0&&destroyed==destroyed_before+(fault==1?1:0),"unreferenced prepare failure leaked or quarantined");
        check(SUCCEEDED(f.command->Close()),"close empty own recording");return;
    }
    check(sources.contains(retained)&&!retained->cpu_active&&sources.held(f.upload.Get())&&sources.held(f.output.Get()),"recorded references released at CPU end");
    check(slot_reserved&&blocked==(fault!=6),"failed recording regained slot authority");
    if(fault==4)check(allocation_attempts==attempts_before+1,"publication did not force a real allocator exception");
    if(fault==2)check(SUCCEEDED(f.command->Close()),"close partial empty recording");
    sources.after_close(f.command.Get(),1,S_OK);
    auto* raw_source=f.upload.Get();auto* raw_output=f.output.Get();
    ID3D12CommandList* lists[]{f.command.Get()};
    auto batch=sources.before_execute(f.queue.Get(),1,lists,[](auto*){return uint64_t{1};});
    check(batch.size()==(fault<4?0u:1u),"unknown recording manufactured a submission receipt");
    if(fault!=2){
        check(SUCCEEDED(f.queue->Wait(f.gate.Get(),1)),"gate own GPU copy");
        f.queue->ExecuteCommandLists(1,lists);++gpu_copies;sources.after_execute(batch);
        const auto signal=f.queue->Signal(f.shared.Get(),1);check(signal==S_OK,"actual own producer signal");sources.after_signal(batch,true,f.shared.Get(),1);
    }
    f.upload.Reset();f.output.Reset();
    check(sources.held(raw_source)&&sources.held(raw_output)&&raw_output->GetDesc().Width==512,"caller refs dropped before own retention");
    // Reset the list using a DIFFERENT allocator; never reset memory still used by the GPU.
    ComPtr<ID3D12CommandAllocator> fresh;check(SUCCEEDED(f.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&fresh))),"new own allocator");
    const auto reset=f.command->Reset(fresh.Get(),nullptr);check(reset==S_OK,"actual discard/seal Reset");sources.after_reset(f.command.Get(),1,reset);
    check(sources.collect()==0&&sources.size()==1&&destroyed==destroyed_before,"Reset or CPU completion retired blocked/unknown GPU use");
    if(fault!=2){
        f.unblock();ComPtr<ID3D12Resource> result_resource=raw_output;f.pixels(result_resource.Get());
        if(fault>=4)check(sources.collect()==1&&sources.size()==0&&destroyed==destroyed_before+1,"exact reset/fence did not retire completed copy");
        else check(sources.collect()==0&&sources.size()==1,"partial-record quarantine was guessed safe");
    }
    check(SUCCEEDED(f.command->Close()),"final own close");
}
void scope_cases(){
    for(bool initial:{false,true})for(unsigned fault=0;fault<3;++fault){
        bool flag=initial;unsigned finish{},fail{},observed{},original_calls{};
        // The observer is invoked only AFTER the independent original call.
        ++original_calls;
        w3vr::capture_recording::observe([&]{
            ++observed;const auto result=w3vr::capture_recording::run(flag,[]{return true;},[&]{
                check(flag,"nested outer flag");
                unsigned inner_finish{},inner_fail{};
                check(!w3vr::capture_recording::run(flag,[]{return false;},[]{},[]{return true;},
                    [&](bool referenced)noexcept{++inner_fail;if(referenced)std::abort();},[&]()noexcept{++inner_finish;}) &&
                    flag&&inner_fail==1&&inner_finish==1,"nested scope restored wrong flag or replayed work");
                if(fault==0)throw std::bad_alloc();
            },[&]{if(fault==1)throw std::bad_alloc();return true;},[&](bool)noexcept{++fail;},[&]()noexcept{++finish;});
            check(result==(fault==2),"scope result");
            throw std::bad_alloc(); // Also verify the surrounding observation boundary.
        },[&]()noexcept{++fail;});
        check(flag==initial&&finish==1&&fail==(fault==2?1u:2u)&&observed==1&&original_calls==1,"scope exception escaped or original was repeated");
    }
}
int main(){for(unsigned fault=0;fault<7;++fault)recording_case(fault);scope_cases();
    std::cout<<checks<<" checks; "<<gpu_copies<<" real own GPU copies, "<<gpu_copies*512<<" verified bytes; seven prepare/record/publish outcomes, real Reset and fences, numeric frame IDs fabricated. Own guard and Store used; Host callbacks/Game/SDK/OpenXR runtime/Quest NOT executed.\n";
}
