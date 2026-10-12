#pragma once
#include "modern_graphics_binding.h"
#include "modern_eye_pair.h"
#include <vector>
#include <algorithm>

namespace w3vr::modern_backbuffer_source {
using Microsoft::WRL::ComPtr;
struct Key {
    modern_eye_capture::Identity identity{};
    std::uint64_t recording_epoch{},serial{};
    std::uint32_t slot{UINT32_MAX};
};
struct Submission {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    std::uint64_t nonce{},value{UINT64_MAX};
    bool execute_returned{},result_observed{},signal_ok{};
};
struct Record {
    modern_graphics_binding::Snapshot binding;
    ComPtr<ID3D12Resource> source,destination;
    ComPtr<ID3D12GraphicsCommandList> command;
    Key key;
    bool cpu_active{true},recorded{},sealed{},quarantined{};
    std::vector<Submission> submissions;
    Record* next{};
    virtual ~Record()=default;
    // No Native allocator or engine object ownership is inferred here.
};
struct Ticket {Record* record{};std::uint64_t nonce{};};
using Batch=std::vector<Ticket>;
// Caller serializes metadata with the same capture mutex. POD root deliberately
// has no submitted-COM destructor in DllMain. This is not unload/all-call safety.
struct Store {
    Record* head{};
    std::uint64_t nonce{};
    bool contains(const Record* target)const{
        for(auto* r=head;r;r=r->next)if(r==target)return true;return false;
    }
    bool held(ID3D12Resource* resource)const{
        if(!resource)return false;
        for(auto* r=head;r;r=r->next)if(r->source.Get()==resource || r->destination.Get()==resource)return true;
        return false;
    }
    std::size_t size()const{std::size_t n{};for(auto* r=head;r;r=r->next)++n;return n;}
    Record* adopt(std::unique_ptr<Record> r){
        if(!r || !r->binding || !modern_eye_pair::finite(r->binding->epoch) || !r->source || !r->destination ||
            !r->command || r->source.Get()==r->destination.Get() ||
            !modern_eye_pair::known(r->key.identity.profile) || !r->key.identity.renderer ||
            !r->key.identity.frame.frame || !r->key.identity.frame.generation ||
            !modern_eye_pair::finite(r->key.identity.frame.pair) || r->key.identity.frame.eye<0 ||
            r->key.identity.frame.eye>1 || !r->key.identity.frame.view_valid ||
            !r->key.identity.frame.normal_factory_lineage || !modern_eye_pair::finite(r->key.recording_epoch) ||
            !modern_eye_pair::finite(r->key.serial) || r->key.slot>=3)return nullptr;
        r->next=head;head=r.release();return head;
    }
    void mark_recorded(Record* r){if(contains(r))r->recorded=true;}
    void finish_cpu(Record* r){if(contains(r)){r->cpu_active=false;collect();}}
    std::size_t collect(){
        std::size_t count{};auto** link=&head;
        while(*link){auto* r=*link;
            bool retired=!r->cpu_active && !r->quarantined && (!r->recorded || r->sealed);
            for(const auto& use:r->submissions){
                const auto completed=use.signal_ok && use.fence ? use.fence->GetCompletedValue():UINT64_MAX;
                retired=retired && use.execute_returned && use.result_observed && use.signal_ok &&
                    modern_eye_pair::finite(use.value) && completed!=UINT64_MAX && completed>=use.value;
            }
            if(retired){*link=r->next;delete r;++count;}else link=&r->next;
        }return count;
    }
    template<class EpochReader>
    Batch before_execute(ID3D12CommandQueue* actual_queue,UINT actual_count,
        ID3D12CommandList* const* actual_lists,EpochReader&& epoch){
        Batch batch;
        if(!actual_queue || !actual_lists || !actual_count)return batch;
        ComPtr<ID3D12Device> device;
        auto queue_key=FAILED(actual_queue->GetDevice(IID_PPV_ARGS(&device))) ? ComPtr<IUnknown>{}:
            modern_graphics_binding::identity(device.Get());
        for(auto* r=head;r;r=r->next){
            if(!r->recorded || r->sealed || std::find(actual_lists,actual_lists+actual_count,r->command.Get())==actual_lists+actual_count)continue;
            // Observation rejection cannot discard references of a list that
            // the Original still receives. Unknown use remains quarantined.
            if(r->quarantined || !queue_key || actual_queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
                queue_key.Get()!=r->binding->device_identity.Get() ||
                epoch(r->command.Get())!=r->key.recording_epoch || nonce>=UINT64_MAX-1){
                r->quarantined=true;continue;
            }
            try{
                Submission use;use.queue=actual_queue;use.nonce=++nonce;
                r->submissions.push_back(std::move(use));batch.push_back({r,nonce});
            }catch(...){r->quarantined=true;}
        }return batch;
    }
    Submission* find(const Ticket& ticket){
        if(!contains(ticket.record))return nullptr;
        auto& uses=ticket.record->submissions;
        const auto found=std::find_if(uses.begin(),uses.end(),[&](const auto& use){return use.nonce==ticket.nonce;});
        return found==uses.end()?nullptr:&*found;
    }
    void after_execute(const Batch& batch){for(const auto& ticket:batch)if(auto* use=find(ticket))use->execute_returned=true;}
    void after_signal(const Batch& batch,bool original_succeeded,ID3D12Fence* actual_fence,std::uint64_t actual_value){
        for(const auto& ticket:batch){auto* use=find(ticket);
            if(!use || !use->execute_returned || use->result_observed)continue;
            use->result_observed=true;
            if(!original_succeeded || !actual_fence || !modern_eye_pair::finite(actual_value))continue;
            ComPtr<ID3D12Device> device;
            if(FAILED(actual_fence->GetDevice(IID_PPV_ARGS(&device))))continue;
            auto key=modern_graphics_binding::identity(device.Get());
            if(!key || key.Get()!=ticket.record->binding->device_identity.Get())continue;
            use->fence=actual_fence;use->value=actual_value;use->signal_ok=true;
        }
    }
    void after_reset(ID3D12GraphicsCommandList* actual_command,std::uint64_t discarded_epoch,HRESULT original_result){
        if(!actual_command || FAILED(original_result) || !modern_eye_pair::finite(discarded_epoch))return;
        for(auto* r=head;r;r=r->next)
            if(r->command.Get()==actual_command && r->key.recording_epoch==discarded_epoch)r->sealed=true;
        collect();
    }
    // A separately returned drain signal AFTER Original returned can recover
    // only known failed-signal uses on that exact queue. Host does not invoke
    // this recovery yet. Unknown epochs/queues are never repaired by a counter.
    void after_drain(ID3D12CommandQueue* actual_queue,HRESULT original_signal_result,
        ID3D12Fence* actual_fence,std::uint64_t actual_value){
        if(!actual_queue || FAILED(original_signal_result) || !actual_fence || !modern_eye_pair::finite(actual_value))return;
        for(auto* r=head;r;r=r->next)if(!r->quarantined){
            ComPtr<ID3D12Device> device;
            if(FAILED(actual_fence->GetDevice(IID_PPV_ARGS(&device))))continue;
            auto key=modern_graphics_binding::identity(device.Get());
            if(!key || key.Get()!=r->binding->device_identity.Get())continue;
            for(auto& use:r->submissions)if(use.queue.Get()==actual_queue && use.execute_returned &&
                use.result_observed && !use.signal_ok){use.fence=actual_fence;use.value=actual_value;use.signal_ok=true;}
        }
    }
    template<class EpochReader,class Original>
    Batch execute_from_callback(std::mutex& capture_mutex,ID3D12CommandQueue* actual_queue,UINT actual_count,
        ID3D12CommandList* const* actual_lists,EpochReader&& epoch,Original&& original){
        Batch batch;{std::lock_guard lock{capture_mutex};batch=before_execute(actual_queue,actual_count,actual_lists,epoch);}
        original(); // Exactly once; observation never suppresses the Native call.
        {std::lock_guard lock{capture_mutex};after_execute(batch);}return batch;
    }
};
} // namespace w3vr::modern_backbuffer_source
