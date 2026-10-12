#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>

namespace w3vr::modern_graphics_binding {
using Microsoft::WRL::ComPtr;
inline constexpr UINT max_buffers=16;
struct Data {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<IUnknown> device_identity,queue_identity,chain_identity;
    std::uint64_t epoch{};
    UINT width{},height{},buffer_count{},present_count{};
    std::array<ComPtr<ID3D12CommandQueue>,max_buffers> present_queues;
    std::array<ComPtr<IUnknown>,max_buffers> present_identities;
    std::array<UINT,max_buffers> creation_masks{};
    bool explicit_present_queues{};

};
using Snapshot=std::shared_ptr<const Data>;
struct Backbuffer {Snapshot binding;ComPtr<ID3D12Resource> resource;ComPtr<ID3D12CommandQueue> present_queue;UINT index{UINT_MAX};};
inline bool finite(std::uint64_t value){return value && value!=UINT64_MAX;}
inline ComPtr<IUnknown> identity(IUnknown* object){
    ComPtr<IUnknown> result;
    if(object && FAILED(object->QueryInterface(IID_PPV_ARGS(&result))))result.Reset();
    return result;
}
// Callback arguments must already be valid owned/borrowed COM interfaces.
// This does not repair an unsafe pointer before entry, Native ownership, or
// module/loader/all-call lifetime. No COM call occurs while mutex_ is held.
class Registry {
    mutable std::mutex mutex_;
    std::recursive_mutex resize_gate_;
    std::atomic<bool> accepting_{true};
    Snapshot active_;
    std::uint64_t epoch_{};
    bool resizing_{};
    struct Resize {Snapshot original;std::uint64_t stamp{};};
    Resize begin_resize(IDXGISwapChain* chain){
        auto key=identity(chain);
        std::lock_guard lock{mutex_};
        if(!accepting_.load(std::memory_order_acquire) || !active_ || !key ||
            key.Get()!=active_->chain_identity.Get() || resizing_)return {};
        if(epoch_>=UINT64_MAX-1){accepting_.store(false,std::memory_order_release);return {};}
        resizing_=true;return {active_,++epoch_};
    }
    struct QueueSet {
        std::array<ComPtr<ID3D12CommandQueue>,max_buffers> queues;
        std::array<ComPtr<IUnknown>,max_buffers> identities;
        std::array<UINT,max_buffers> masks{};
        UINT count{};
    };
    QueueSet stage_queues(const Resize& ticket,UINT requested_count,const UINT* masks,IUnknown* const* queues){
        QueueSet result;
        if(!ticket.original || !queues)return result;
        // The public arrays are sized by requested_count. A zero count gives
        // no readable extent here; GetDesc cannot enlarge that borrowed array.
        const UINT count=requested_count;
        if(!count || count>max_buffers)return result;
        for(UINT i=0;i<count;++i){
            ComPtr<ID3D12Device> device;
            if(!queues[i] || FAILED(queues[i]->QueryInterface(IID_PPV_ARGS(&result.queues[i]))) ||
                result.queues[i]->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
                FAILED(result.queues[i]->GetDevice(IID_PPV_ARGS(&device))))return {};
            auto device_key=identity(device.Get());result.identities[i]=identity(result.queues[i].Get());
            if(!device_key || !result.identities[i] || device_key.Get()!=ticket.original->device_identity.Get())return {};
            result.masks[i]=masks?masks[i]:0;
        }
        result.count=count;return result;
    }
    void finish_resize(const Resize& ticket,HRESULT original_result,const QueueSet* explicit_queues=nullptr){
        if(!ticket.original)return;
        // These added COM references and descriptor calls happen outside the
        // registry mutex. Existing snapshots keep the old Data alive.
        auto next=std::make_shared<Data>(*ticket.original);
        if(SUCCEEDED(original_result)){
            DXGI_SWAP_CHAIN_DESC1 desc{};
            if(FAILED(next->chain->GetDesc1(&desc)) || !desc.Width || !desc.Height){
                stop_no_release();return;
            }
            next->width=desc.Width;next->height=desc.Height;next->buffer_count=desc.BufferCount;
            if(explicit_queues){
                next->explicit_present_queues=true;next->present_count=0;
                next->present_queues={};next->present_identities={};next->creation_masks={};
                if(explicit_queues->count==desc.BufferCount && desc.BufferCount && desc.BufferCount<=max_buffers){
                    next->present_count=desc.BufferCount;next->present_queues=explicit_queues->queues;
                    next->present_identities=explicit_queues->identities;next->creation_masks=explicit_queues->masks;
                    next->queue=next->present_queues[0];next->queue_identity=next->present_identities[0];
                }
            }else if(next->explicit_present_queues){
                // Plain ResizeBuffers supplies no new queue array. After an
                // explicit remap its queue association is not proven here.
                next->present_count=0;next->present_queues={};next->present_identities={};
            }else{
                next->present_count=desc.BufferCount<=max_buffers?desc.BufferCount:0;
                for(UINT i=0;i<next->present_count;++i){next->present_queues[i]=next->queue;next->present_identities[i]=next->queue_identity;}
            }
        }
        Snapshot previous;
        {std::lock_guard lock{mutex_};
            if(!accepting_.load(std::memory_order_acquire) || !resizing_ ||
                active_.get()!=ticket.original.get() || epoch_!=ticket.stamp)return;
            next->epoch=ticket.stamp;previous=std::move(active_);active_=std::move(next);resizing_=false;
        }
    }
public:
    // Pair the ORIGINAL factory input queue and its successful output chain.
    // Same device alone does not infer which queue created a foreign chain.
    bool publish_factory(HRESULT original_result,IUnknown* queue_argument,IDXGISwapChain* chain_argument){
        if(FAILED(original_result) || !queue_argument || !chain_argument ||
            !accepting_.load(std::memory_order_acquire))return false;
        auto next=std::make_shared<Data>();
        ComPtr<ID3D12Device> chain_device;
        if(FAILED(queue_argument->QueryInterface(IID_PPV_ARGS(&next->queue))) || !next->queue ||
            next->queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
            FAILED(next->queue->GetDevice(IID_PPV_ARGS(&next->device))) || !next->device ||
            FAILED(chain_argument->QueryInterface(IID_PPV_ARGS(&next->chain))) || !next->chain ||
            FAILED(next->chain->GetDevice(IID_PPV_ARGS(&chain_device))) || !chain_device)return false;
        next->device_identity=identity(next->device.Get());next->queue_identity=identity(next->queue.Get());
        next->chain_identity=identity(next->chain.Get());auto chain_device_key=identity(chain_device.Get());
        DXGI_SWAP_CHAIN_DESC1 desc{};
        if(!next->device_identity || !next->queue_identity || !next->chain_identity ||
            !chain_device_key || chain_device_key.Get()!=next->device_identity.Get() ||
            FAILED(next->chain->GetDesc1(&desc)) || !desc.Width || !desc.Height)return false;
        if(!desc.BufferCount || desc.BufferCount>max_buffers)return false;
        next->width=desc.Width;next->height=desc.Height;next->buffer_count=desc.BufferCount;
        next->present_count=desc.BufferCount;
        for(UINT i=0;i<desc.BufferCount;++i){next->present_queues[i]=next->queue;next->present_identities[i]=next->queue_identity;}
        {std::lock_guard lock{mutex_};
            if(!accepting_.load(std::memory_order_acquire) || resizing_)return false;
            // Replacing a different game chain needs a proven drain protocol.
            // Do not silently adopt an auxiliary chain or retarget live XR.
            if(active_)return active_->chain_identity.Get()==next->chain_identity.Get() &&
                active_->queue_identity.Get()==next->queue_identity.Get() &&
                active_->device_identity.Get()==next->device_identity.Get();
            if(epoch_>=UINT64_MAX-1){accepting_.store(false,std::memory_order_release);return false;}
            next->epoch=++epoch_;active_=std::move(next);
        }return true;
    }
    Snapshot acquire()const{
        Snapshot result;
        {std::lock_guard lock{mutex_};
            if(accepting_.load(std::memory_order_acquire) && !resizing_ && active_ && active_->present_count &&
                active_->present_count==active_->buffer_count)result=active_;
        }
        if(!accepting_.load(std::memory_order_acquire))result.reset();return result;
    }
    bool current(const Snapshot& snapshot)const{
        std::lock_guard lock{mutex_};
        return accepting_.load(std::memory_order_acquire) && !resizing_ && snapshot && active_ &&
            snapshot.get()==active_.get() && snapshot->present_count && snapshot->present_count==snapshot->buffer_count &&
            finite(snapshot->epoch) && snapshot->epoch==epoch_;
    }
    bool ready()const{return current(acquire());}
    bool owns_child(const Snapshot& snapshot,ID3D12DeviceChild* child)const{
        if(!snapshot || !child || !current(snapshot))return false;
        ComPtr<ID3D12Device> device;
        if(FAILED(child->GetDevice(IID_PPV_ARGS(&device))) || !device)return false;
        auto key=identity(device.Get());return key && key.Get()==snapshot->device_identity.Get() && current(snapshot);
    }
    Backbuffer acquire_backbuffer()const{
        Backbuffer result;result.binding=acquire();if(!result.binding)return {};
        const auto index=result.binding->chain->GetCurrentBackBufferIndex();
        if(index>=result.binding->present_count || !result.binding->present_queues[index])return {};
        result.index=index;result.present_queue=result.binding->present_queues[index];
        if(FAILED(result.binding->chain->GetBuffer(index,IID_PPV_ARGS(&result.resource))) ||
            !result.resource || !owns_child(result.binding,result.resource.Get()))return {};
        return result;
    }
    template<class Original>
    HRESULT resize_from_callback(IDXGISwapChain* chain,Original&& original){
        // Serialize observed resize callbacks, but never hold the snapshot
        // mutex across the Original API call. Forward exactly once.
        std::lock_guard resize_lock{resize_gate_};auto ticket=begin_resize(chain);
        const HRESULT result=original();
        try{finish_resize(ticket,result);}catch(...){stop_no_release();}
        return result;
    }
    template<class Original>
    HRESULT resize1_from_callback(IDXGISwapChain3* chain,UINT requested_count,
        const UINT* creation_masks,IUnknown* const* present_queues,Original&& original){
        std::lock_guard resize_lock{resize_gate_};auto ticket=begin_resize(chain);
        QueueSet staged;
        try{staged=stage_queues(ticket,requested_count,creation_masks,present_queues);}catch(...){staged={};}
        const HRESULT result=original(); // Preserve actual arrays/arguments exactly once.
        try{finish_resize(ticket,result,&staged);}catch(...){stop_no_release();}
        return result;
    }
    // Atomic revocation only: safe to request without COM/locks from DllMain.
    // Host deliberately retains the registry root. This is NOT unload safety.
    void stop_no_release(){accepting_.store(false,std::memory_order_release);}
};
} // namespace w3vr::modern_graphics_binding
