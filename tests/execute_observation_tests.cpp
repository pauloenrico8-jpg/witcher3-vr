#include "xr_allocator_submissions.h"
#include "modern_capture_recording.h"
#include "execute_observation.h"
#include "modern_dlss_native_hooks.h"
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
namespace guard=w3vr::execute_observation;
namespace hooks=w3vr::modern_dlss_native_hooks;
unsigned destroyed{},gpu_copies{},allocation_attempts{};
struct Tracked:source::Record{~Tracked(){++destroyed;}};
template<class T>struct FailingAllocator{
    using value_type=T;FailingAllocator()=default;template<class U>FailingAllocator(const FailingAllocator<U>&){}
    T* allocate(std::size_t){++allocation_attempts;throw std::bad_alloc();}
    void deallocate(T*,std::size_t)noexcept{}bool operator==(const FailingAllocator&)const{return true;}
};
void allocation_failure(){std::vector<int,FailingAllocator<int>> records;records.push_back(1);}
struct RawContext{
    Fixture* fixture{};std::mutex* mutex{};ID3D12CommandQueue* queue{};
    ID3D12CommandList* const* array{};UINT count{};unsigned calls{},fault{};
};
thread_local RawContext* raw_context{};
// OWN throwing callback; the actual system Execute is still called once.
// Throwing here is a simulated original callback failure, NOT a driver failure.
void STDMETHODCALLTYPE own_raw(ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* array){
    auto& c=*raw_context;++c.calls;
    check(c.calls==1&&queue==c.queue&&count==c.count&&array==c.array,"raw original repeated or exact arguments changed");
    check(c.mutex->try_lock(),"raw original ran under capture mutex");c.mutex->unlock();
    queue->ExecuteCommandLists(count,array);++gpu_copies;
    if(c.fault==2)throw std::bad_alloc();
}
source::Record* retain(source::Store& store,Fixture& f,ID3D12GraphicsCommandList* command,unsigned serial){
    auto data=std::make_shared<w3vr::modern_graphics_binding::Data>();data->epoch=1;data->device=f.device;
    data->device_identity=w3vr::modern_graphics_binding::identity(f.device.Get());data->queue=f.queue;
    auto r=std::make_unique<Tracked>();r->binding=data;r->source=f.upload;r->destination=f.output;r->command=command;
    r->key.identity={&w3vr::render_core::remastered_1048522,0x1000,{0x2000,7,1,0,true,true,1000,{0x3000,1,1}}};
    r->key.recording_epoch=1;r->key.serial=serial;r->key.slot=0;
    auto* result=store.adopt(std::move(r));check(result!=nullptr,"own source record admission");
    store.mark_recorded(result);store.finish_cpu(result);store.after_close(command,1,S_OK);return result;
}
void execution_case(unsigned fault){
    Fixture f;source::Store store;std::mutex mutex;
    check(SUCCEEDED(f.allocator->Reset())&&SUCCEEDED(f.command->Reset(f.allocator.Get(),nullptr)),"actual primary Reset");f.copy();
    ComPtr<ID3D12CommandAllocator> empty_allocator,unrelated_allocator,fresh;
    ComPtr<ID3D12GraphicsCommandList> empty,unrelated;
    auto new_list=[&](auto& allocator,auto& list){
        check(SUCCEEDED(f.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))&&
            SUCCEEDED(f.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))&&
            SUCCEEDED(list->Close()),"own extra closed list");
    };
    new_list(empty_allocator,empty);new_list(unrelated_allocator,unrelated);
    check(SUCCEEDED(f.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&fresh))),"fresh allocator for discard");
    auto* record=retain(store,f,f.command.Get(),1);auto* other=retain(store,f,unrelated.Get(),2);
    ID3D12CommandList* array[]{f.command.Get(),empty.Get()};
    RawContext c{&f,&mutex,f.queue.Get(),array,2,0,fault};raw_context=&c;
    auto* target=(*reinterpret_cast<void***>(f.queue.Get()))[10];
    // Unbound data path is exercised with an OWN original callback. This is
    // not a successful MinHook installation receipt or a Game endpoint proof.
    auto delegate=hooks::execute_forwarder(target,own_raw);hooks::Forwarder absent;
    check(bool(delegate),"own delegate storage");
    check(SUCCEEDED(f.queue->Wait(f.gate.Get(),1)),"actual own GPU gate");
    bool open{},reset{},failed{},published{};unsigned finishes{};source::Batch batch;
    guard::Outcome outcome{};
    {
        auto finish=[&]()noexcept{
            ++finishes;std::lock_guard lock{mutex};if(open)store.end_execute_observation();store.collect();
        };
        w3vr::capture_recording::CpuScope cpu{finish};
        outcome=guard::run([&](guard::Receipt& receipt){
            {std::lock_guard lock{mutex};store.begin_execute_observation();open=true;}
            if(fault==0){std::lock_guard lock{mutex};allocation_failure();}
            {std::lock_guard lock{mutex};batch=store.before_execute(f.queue.Get(),2,array,[](auto*){return uint64_t{1};});}
            if(fault==4){absent.execute(f.queue.Get(),2,array,&receipt);receipt.require_returned();}
            if(fault==5)return; // Missing forwarding must fail closed before fallback.
            delegate.execute(f.queue.Get(),2,array,&receipt);receipt.require_returned();
            if(fault==7){delegate.execute(f.queue.Get(),2,array,&receipt);check(c.calls==1,"delegate repeated a consumed receipt");}
            if(fault==3){
                // An actual Reset races the missing post-Execute metadata.
                // This new allocator is independent of the GPU-blocked one.
                const auto hr=f.command->Reset(fresh.Get(),nullptr);check(hr==S_OK,"actual Reset before observer failure");reset=true;
                {std::lock_guard lock{mutex};store.after_reset(f.command.Get(),1,hr);
                    check(store.contains(record)&&store.size()==2,"Reset deleted source during observation");}
                allocation_failure();
            }
            {std::lock_guard lock{mutex};store.after_execute(batch);}
            const auto hr=f.queue->Signal(f.shared.Get(),1);check(hr==S_OK,"actual post-Execute Signal");
            {std::lock_guard lock{mutex};store.after_signal(batch,true,f.shared.Get(),1);}
            if(fault==1)allocation_failure();
            published=true;
        },[&]{own_raw(f.queue.Get(),2,array);},[&](guard::Phase)noexcept{
            failed=true;published=false;std::lock_guard lock{mutex};store.quarantine_execute(2,array);
        });
    }
    check(c.calls==1&&finishes==1&&store.execute_observations==0,"forward count or observation hold leaked");
    check(outcome.phase==(fault==2?guard::Phase::Entered:guard::Phase::Returned),"original entry/return identity lost");
    check(outcome.observation_failed==(fault<6)&&failed==(fault<6)&&published==(fault>=6),"failure published an incomplete result");
    check(store.contains(record)&&record->quarantined==(fault<6)&&!other->quarantined,"quarantine lost exact array identity");
    check(SUCCEEDED(unrelated->Reset(unrelated_allocator.Get(),nullptr)),"actual unrelated discard Reset");
    store.after_reset(unrelated.Get(),1,S_OK);check(!store.contains(other),"unrelated recording was quarantined");
    if(!reset){const auto hr=f.command->Reset(fresh.Get(),nullptr);check(hr==S_OK,"actual post-observation discard Reset");store.after_reset(f.command.Get(),1,hr);}
    auto* raw_source=f.upload.Get();auto* raw_output=f.output.Get();f.upload.Reset();f.output.Reset();
    check(store.collect()==0&&store.contains(record)&&store.held(raw_source)&&store.held(raw_output),"Reset/CPU end released possibly submitted work");
    f.unblock();f.pixels(raw_output);
    if(fault<6)check(store.collect()==0&&store.contains(record),"unknown observation guessed safe after drain");
    else check(store.collect()==1&&!store.contains(record),"exact returned Execute/Reset/fence did not retire successful work");
    check(SUCCEEDED(f.command->Close())&&SUCCEEDED(unrelated->Close()),"final own list Close");raw_context=nullptr;
    std::cout<<"case="<<fault<<" raw_calls="<<c.calls<<" returned="<<(outcome.phase==guard::Phase::Returned)
        <<" contained="<<outcome.observation_failed<<" copies=1\n";
}
void publication_scope_cases(){
    for(unsigned fault=0;fault<5;++fault){
        guard::Receipt receipt;unsigned publish{},failed{},raw_calls{};
        auto finish=[&]()noexcept{
            guard::after_returned(receipt,0,[&]{++publish;if(fault==4)allocation_failure();},[&]()noexcept{++failed;});
        };
        try{
            w3vr::capture_recording::CpuScope scope{finish};
            if(fault!=0)receipt.forward_raw([&]{++raw_calls;if(fault==1)throw std::bad_alloc();});
            if(fault==2)allocation_failure(); // raw returned, but destructor runs during unwind.
        }catch(const std::bad_alloc&){}
        check(raw_calls==(fault==0?0u:1u),"publication scope altered original forwarding");
        check(publish==(fault>=3?1u:0u)&&failed==(fault==3?0u:1u),"destructor published without raw return or during unwind");
    }
}
void hold_cases(){
    source::Store store;store.begin_execute_observation();store.begin_execute_observation();
    check(store.execute_observations==2&&store.collect()==0,"nested observation hold");store.end_execute_observation();
    check(store.execute_observations==1,"inner hold removed outer hold");store.end_execute_observation();store.end_execute_observation();
    check(store.execute_observations==0,"hold underflow");
    store.execute_observations=SIZE_MAX;store.begin_execute_observation();store.end_execute_observation();
    check(store.observation_overflow&&store.execute_observations==SIZE_MAX&&store.collect()==0,"overflow manufactured retirement");
    unsigned calls{},failures{};
    const auto result=guard::run([](auto& receipt){receipt.forward_raw([]{throw std::bad_alloc();});},[&]{++calls;},
        [&](auto phase)noexcept{++failures;if(phase!=guard::Phase::Entered)std::abort();});
    check(calls==0&&failures==1&&result.phase==guard::Phase::Entered,"throwing raw invocation was retried");
}
int main(){for(unsigned fault=0;fault<8;++fault)execution_case(fault);hold_cases();publication_scope_cases();
    std::cout<<checks<<" checks; "<<gpu_copies<<" actual OWN GPU copies; "<<gpu_copies*512
        <<" verified bytes; "<<allocation_attempts<<" allocator failures. Raw callback throws simulated; frame IDs fabricated. "
        <<"HOST/Game/SDK/OpenXR runtime/Quest NOT executed; unbound Forwarder, no new MinHook installation.\n";
}
