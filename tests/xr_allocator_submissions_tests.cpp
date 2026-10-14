#include "xr_allocator_submissions.h"
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
void completed_path(){
    Fixture f;a::Store store(signal_callback);f.reset(store);f.copy();
    w3vr::modern_eye_source_use::Store sources;
    auto source_use=std::make_unique<w3vr::modern_eye_source_use::Use>();
    source_use->textures[0]=f.upload;source_use->textures[1]=f.output;source_use->queue=f.queue;
    source_use->submission_fence=f.shared;source_use->fence=f.shared;source_use->recording=f.command;source_use->allocator=f.allocator;source_use->recorded=true;
    auto* use=sources.adopt(std::move(source_use));
    check(sources.submission_matches(use,f.queue.Get(),f.shared.Get())&&
        !sources.submission_matches(use,f.other.Get(),f.shared.Get()),"shared submission identity before execution");
    auto t=store.prepare(0,f.objects());check(bool(t.anchor)&&store.size()==1,"prepare retains exact objects");
    auto replay=t;replay.nonce++;ID3D12CommandList* lists[]{f.command.Get()};
    check(!store.before_execute(replay,f.queue.Get(),1,lists)&&!store.before_execute(t,f.other.Get(),1,lists)&&
        !store.before_execute(t,f.queue.Get(),0,lists),"foreign ticket/queue/array admitted");
    check(SUCCEEDED(f.queue->Wait(f.gate.Get(),1)),"own queue gated");
    check(sources.before_execute(use,f.queue.Get(),1,lists)&&store.before_execute(t,f.queue.Get(),1,lists),"own execution marker");
    check(!store.cancel(t)&&!store.before_execute(t,f.queue.Get(),1,lists),"executing cancelled/replayed");
    f.queue->ExecuteCommandLists(1,lists);const auto original=f.queue->Signal(f.shared.Get(),5);unsigned prior=signal_calls;
    check(store.after_shared_signal(t,f.queue.Get(),f.shared.Get(),original,5)&&signal_calls==prior+1,"own private signal");
    check(!store.after_shared_signal(t,f.queue.Get(),f.shared.Get(),original,5)&&signal_calls==prior+1,"duplicate private signal");
    const auto receipt=store.receipt(t);check(receipt.fence&&receipt.value==1&&receipt.fence.Get()!=f.shared.Get(),"private source retirement receipt");
    use->fence=receipt.fence;check(sources.after_signal(use,original,receipt.value),"source private marker");
    check(sources.submission_matches(use,f.queue.Get(),f.shared.Get())&&
        !sources.submission_matches(use,f.queue.Get(),receipt.fence.Get()),"private retirement invalidated shared submission identity");
    sources.finish_cpu(use);check(!sources.submission_matches(use,f.queue.Get(),f.shared.Get()),"ended CPU source remained current");
    auto* retained_output=f.output.Get();f.output.Reset();check(retained_output->GetDesc().Width==512,"caller dropped resource ref");
    store.finish_cpu(t);
    check(SUCCEEDED(f.shared->Signal(900))&&f.shared->GetCompletedValue()==900,"advance unrelated shared marker while GPU blocked");
    check(!store.wait(0,f.allocator.Get(),20)&&store.size()==1&&store.collect()==0&&sources.collect()==0&&sources.held(retained_output),"shared fence retired private GPU use or sources");
    auto premature_objects=f.objects(2,6);premature_objects.destination=retained_output;auto premature=store.prepare(0,std::move(premature_objects));check(!premature.anchor,"slot overwritten while submitted");
    f.unblock();check(store.wait(0,f.allocator.Get(),5000)&&store.size()==1&&store.collect()==0,"completed but replayable refs released");
    ComPtr<ID3D12Resource> output=retained_output;f.pixels(output.Get());
    check(sources.collect()==1&&!sources.held(retained_output),"sources not retired by actual private fence");
    f.reset(store);check(store.size()==0&&store.known_slot(0)&&store.wait(0,f.allocator.Get(),0),"successful reset did not seal completed old recording");
    f.output=output;check(SUCCEEDED(f.command->Close()),"close reused list");
    auto next=store.prepare(0,f.objects(2,6));check(next.anchor&&next.nonce!=t.nonce,"new own epoch slot");
    store.finish_cpu(t);check(store.phase(next)==a::Phase::Prepared,"stale finish removed current use");
    check(store.cancel(next),"cancel only before own Execute");check(!store.cancel(next)&&!store.before_execute(next,f.queue.Get(),1,lists),"cancelled ticket rearmed execution");store.finish_cpu(next);check(store.size()==1,"cancelled closed recording released before Reset");
    f.reset(store);check(store.size()==0&&store.phase(t)==a::Phase::Unknown,"cancelled sealed refs not released");check(SUCCEEDED(f.command->Close()),"final close");
}
void failures(){
    for(unsigned fault=0;fault<4;++fault){
        Fixture f;a::Store store(signal_callback);f.reset(store);f.copy();auto t=store.prepare(0,f.objects());check(bool(t.anchor),"failure preparation");
        ID3D12CommandList* lists[]{f.command.Get()};check(SUCCEEDED(f.queue->Wait(f.gate.Get(),1))&&store.before_execute(t,f.queue.Get(),1,lists),"failure execute gate");
        f.queue->ExecuteCommandLists(1,lists);const auto original=f.queue->Signal(f.shared.Get(),5);check(original==S_OK,"real shared signal");
        fail_signal=fault==0;const auto prior=signal_calls;
        check(!store.after_shared_signal(t,fault==2?f.other.Get():f.queue.Get(),f.shared.Get(),fault==1?E_FAIL:original,fault==3?6:5),"unknown signal admitted");
        check(signal_calls==prior+(fault==0?1:0),"private callback unexpectedly forwarded");fail_signal=false;
        store.finish_cpu(t);check(store.phase(t)==a::Phase::Quarantined&&!store.wait(0,f.allocator.Get(),0)&&!store.cancel(t),"quarantine reused");
        f.unblock();f.reset(store);check(store.collect()==0&&store.size()==1&&!store.wait(0,f.allocator.Get(),0),"reset/drain manufactured recovery of failed signal");
        check(!store.prepare(1,f.objects(2,6)).anchor,"quarantined root retargeted");check(SUCCEEDED(f.command->Close()),"failure final close");
        // Unresolved entry deliberately survives Store destruction. No automatic
        // release from a destructor or a fabricated recovery receipt is tested.
    }
}
int main(){completed_path();failures();std::cout<<checks<<" checks: real own queues, retained interfaces, private submission fences, gated copy, shared-fence advance, successful Reset sealing and 512 verified bytes. XR frame IDs and failed-signal outcomes are fixtures. No SDK, Witcher, OpenXR runtime, host callbacks or Quest executed.\n";}
