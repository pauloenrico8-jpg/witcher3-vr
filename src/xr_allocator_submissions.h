#pragma once
#include "xr_frame_identity.h"
#include "xr_fence_completion.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <new>
#include <utility>
namespace w3vr::xr_allocator {
using Microsoft::WRL::ComPtr;
struct Objects {
    xr_frame::Identity frame{};std::uint32_t generation{};
    std::uint64_t shared_value{};
    ComPtr<ID3D12Device> device;ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> shared_fence;ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> command;
    ComPtr<ID3D12Resource> destination,auxiliary;
};
struct Ticket {ComPtr<IUnknown> anchor;std::uint64_t nonce{};};
enum class Phase { Unknown,Prepared,Executing,Pending,Quarantined };
struct Receipt {ComPtr<ID3D12Fence> fence;std::uint64_t value{};};
using Signal=HRESULT(*)(ID3D12CommandQueue*,ID3D12Fence*,std::uint64_t);
// Host serializes this own route. Root has no destructor that releases entries:
// unresolved references/events survive until explicit proof or process exit.
// This is not a runtime handle, all-call, GPU pixel or module-unload lease.
class Store {
    struct Entry {
        Objects objects;ComPtr<ID3D12Fence> private_fence;ComPtr<IUnknown> anchor;
        HANDLE event{};std::uint64_t nonce{},epoch{};std::size_t slot{};
        Phase phase{Phase::Prepared};bool cpu_active{true},sealed{},cancelled{},signal_called{},event_registered{},event_drained{};
        Entry* next{};
        ~Entry(){if(event)CloseHandle(event);}
    };
    Entry* head_{};std::array<Entry*,3> slots_{};
    std::array<ComPtr<ID3D12CommandAllocator>,3> known_allocators_{};
    ComPtr<IUnknown> command_identity_,device_identity_,queue_identity_;
    xr_frame::Identity previous_{};std::uint64_t epoch_{},nonce_{};bool blocked_{};
    Signal signal_{};
    static ComPtr<IUnknown> identity(IUnknown* p){ComPtr<IUnknown> result;
        if(p && FAILED(p->QueryInterface(IID_PPV_ARGS(&result))))result.Reset();return result;}
    template<class T> static bool same_device(T* p,IUnknown* wanted){
        ComPtr<ID3D12Device> device;if(!p || FAILED(p->GetDevice(IID_PPV_ARGS(&device))))return false;
        auto actual=identity(device.Get());return actual && actual.Get()==wanted;
    }
    Entry* find(const Ticket& t)const {if(!t.anchor || !t.nonce)return nullptr;
        for(auto* e=head_;e;e=e->next)if(e->nonce==t.nonce&&e->anchor.Get()==t.anchor.Get())return e;return nullptr;}
    static HRESULT native_signal(ID3D12CommandQueue* q,ID3D12Fence* f,std::uint64_t n){return q->Signal(f,n);}
    static bool completed(const Entry& e){return e.phase==Phase::Pending && e.private_fence &&
        xr_fence::completed(e.private_fence->GetCompletedValue(),1);}
    void quarantine(Entry& e){e.phase=Phase::Quarantined;blocked_=true;}
public:
    explicit Store(Signal signal=nullptr):signal_(signal?signal:native_signal){}
    Store(const Store&)=delete;Store& operator=(const Store&)=delete;
    bool observe_reset(ID3D12GraphicsCommandList* command,HRESULT original_result){
        if(original_result!=S_OK || !command || epoch_>=UINT64_MAX-1)return false;
        auto actual=identity(command);if(!actual)return false;
        if(command_identity_ && command_identity_.Get()!=actual.Get())return false;
        if(!command_identity_)command_identity_=actual;++epoch_;
        for(auto* e=head_;e;e=e->next)if(e->epoch<epoch_)e->sealed=true;
        collect();return true;
    }
    Ticket prepare(std::size_t slot,Objects objects){
        Ticket out;if(blocked_ || slot>=slots_.size() || slots_[slot] || !epoch_ ||
            !xr_frame::valid(objects.frame) || !objects.generation || !objects.shared_value ||
            (known_allocators_[slot]&&known_allocators_[slot].Get()!=objects.allocator.Get()) ||
            objects.shared_value==UINT64_MAX || nonce_>=UINT64_MAX-1 || !objects.device ||
            !objects.queue || !objects.shared_fence || !objects.allocator || !objects.command || !objects.destination ||
            (xr_frame::valid(previous_)&&!xr_frame::newer(objects.frame,previous_)))return out;
        auto command_id=identity(objects.command.Get()),device_id=identity(objects.device.Get()),queue_id=identity(objects.queue.Get());
        if(!command_id || command_id.Get()!=command_identity_.Get() || !device_id || !queue_id ||
            (device_identity_ && device_identity_.Get()!=device_id.Get()) ||
            (queue_identity_ && queue_identity_.Get()!=queue_id.Get()) ||
            objects.queue->GetDesc().Type!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
            objects.command->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT ||
            !same_device(objects.queue.Get(),device_id.Get()) || !same_device(objects.shared_fence.Get(),device_id.Get()) ||
            !same_device(objects.allocator.Get(),device_id.Get()) || !same_device(objects.command.Get(),device_id.Get()) ||
            !same_device(objects.destination.Get(),device_id.Get()) ||
            (objects.auxiliary&&!same_device(objects.auxiliary.Get(),device_id.Get())))return out;
        auto* e=new(std::nothrow) Entry;if(!e)return out;
        e->objects=std::move(objects);e->nonce=++nonce_;e->epoch=epoch_;e->slot=slot;
        e->next=head_;head_=e;slots_[slot]=e;previous_=e->objects.frame;known_allocators_[slot]=e->objects.allocator;
        device_identity_=device_id;queue_identity_=queue_id;
        if(e->objects.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&e->private_fence))!=S_OK || !e->private_fence ||
            !(e->anchor=identity(e->private_fence.Get())) || !(e->event=CreateEventW(nullptr,FALSE,FALSE,nullptr))){quarantine(*e);return out;}
        out.anchor=e->anchor;out.nonce=e->nonce;return out;
    }
    bool can_execute(const Ticket& t,ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists)const{
        const auto* e=find(t);return e && !blocked_ && e->cpu_active && !e->cancelled && !e->sealed && e->phase==Phase::Prepared &&
            queue==e->objects.queue.Get() && count==1 && lists && lists[0]==e->objects.command.Get();
    }
    bool before_execute(const Ticket& t,ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists){
        if(!can_execute(t,queue,count,lists))return false;
        find(t)->phase=Phase::Executing;return true; // BEFORE the original void Execute
    }
    bool after_shared_signal(const Ticket& t,ID3D12CommandQueue* queue,ID3D12Fence* fence,HRESULT result,std::uint64_t value){
        auto* e=find(t);if(!e || e->phase!=Phase::Executing || e->signal_called)return false;
        e->signal_called=true;
        if(result!=S_OK || queue!=e->objects.queue.Get() || fence!=e->objects.shared_fence.Get() || value!=e->objects.shared_value){quarantine(*e);return false;}
        HRESULT private_result{};try{private_result=signal_(e->objects.queue.Get(),e->private_fence.Get(),1);}
        catch(...){quarantine(*e);throw;}
        if(private_result!=S_OK){quarantine(*e);return false;}e->phase=Phase::Pending;return true;
    }
    Receipt receipt(const Ticket& t)const{auto* e=find(t);return e&&e->phase==Phase::Pending?Receipt{e->private_fence,1}:Receipt{};}
    bool cancel(const Ticket& t){auto* e=find(t);if(!e || e->phase!=Phase::Prepared || !e->cpu_active || e->cancelled)return false;e->cancelled=true;return true;}
    void finish_cpu(const Ticket& t){if(auto* e=find(t)){e->cpu_active=false;if(e->phase==Phase::Prepared)e->cancelled=true;}collect();}
    bool established()const{return bool(queue_identity_);}
    bool blocked()const{return blocked_;} // diagnostic read only
    bool known_slot(std::size_t slot)const{return slot<slots_.size()&&bool(known_allocators_[slot]);}
    bool has_slot(std::size_t slot)const{return slot<slots_.size()&&slots_[slot];}
    bool wait(std::size_t slot,ID3D12CommandAllocator* allocator,DWORD timeout_ms){
        if(blocked_ || slot>=slots_.size() || !allocator)return false;
        auto* e=slots_[slot];if(!e)return !known_allocators_[slot]||known_allocators_[slot].Get()==allocator;
        if(e->objects.allocator.Get()!=allocator || e->cpu_active)return false;
        if(e->phase==Phase::Prepared)return e->cancelled; // no own Execute attempted; Reset still required before releasing refs
        if(e->phase!=Phase::Pending || !e->private_fence || !e->event)return false;
        auto* const fence=e->private_fence.Get();const HANDLE event=e->event;
        auto value=fence->GetCompletedValue();if(value==UINT64_MAX){quarantine(*e);return false;}
        if(xr_fence::completed(value,1) && (!e->event_registered || e->event_drained))return true;
        if(!e->event_registered){
            // One registration per retained event. Failure is ambiguous and
            // keeps the handle/interfaces; never close or retry it implicitly.
            e->event_registered=true;
            if(fence->SetEventOnCompletion(1,event)!=S_OK){quarantine(*e);return false;}
        }
        if(WaitForSingleObject(event,timeout_ms)!=WAIT_OBJECT_0)return false;
        value=fence->GetCompletedValue();
        if(value==UINT64_MAX){quarantine(*e);return false;}
        if(!xr_fence::completed(value,1))return false;
        e->event_drained=true;return true;
    }
    Phase phase(const Ticket& t)const{auto* e=find(t);return e?e->phase:Phase::Unknown;}
    std::size_t size()const{std::size_t n{};for(auto* e=head_;e;e=e->next)++n;return n;}
    std::size_t collect(){std::size_t n{};auto** link=&head_;
        while(*link){auto* e=*link;
            if(!e->cpu_active && e->sealed && ((completed(*e)&&(!e->event_registered||e->event_drained))||(e->phase==Phase::Prepared&&e->cancelled))){
                *link=e->next;if(slots_[e->slot]==e)slots_[e->slot]=nullptr;delete e;++n;
            }else link=&e->next;
        }return n;
    }
};
} // namespace w3vr::xr_allocator
