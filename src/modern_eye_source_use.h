#pragma once
#include "modern_eye_pair.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <memory>

namespace w3vr::modern_eye_source_use {
using Microsoft::WRL::ComPtr;
// Physical interface ownership, separate from Native image receipts. This
// does not freeze contents or establish game/loader/all-call quiescence.
struct Use {
    std::array<ComPtr<ID3D12Resource>,2> textures;
    std::array<ComPtr<ID3D12Fence>,2> producer_fences;
    std::array<ComPtr<ID3D12CommandQueue>,2> producers;
    ComPtr<ID3D12CommandQueue> queue;
    // Submission identity remains the exact shared fence originally signalled.
    // Retirement can switch to a private per-submission fence without changing it.
    ComPtr<ID3D12Fence> submission_fence;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12GraphicsCommandList> recording;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12Resource> xr_destination;
    modern_eye_pair::UsePhase phase{modern_eye_pair::UsePhase::Recording};
    std::uint64_t fence_value{UINT64_MAX};
    bool recorded{},cpu_active{true};
    Use* next{};
    virtual ~Use()=default;
};
// Host must serialize this store with its capture mutex. Raw POD root avoids
// releasing submitted COM objects from a static destructor during DllMain.
// Remaining nodes intentionally stay owned until proof/process termination;
// this is conservative retention, NOT a safe module-unload protocol.
struct Store {
    Use* head{};
    Use* adopt(std::unique_ptr<Use> use){
        if(!use)return nullptr;
        use->next=head;head=use.release();return head;
    }
    bool contains(const Use* target)const{
        for(auto* use=head;use;use=use->next)if(use==target)return true;
        return false;
    }
    bool submission_matches(const Use* use,ID3D12CommandQueue* queue,ID3D12Fence* fence)const{
        return contains(use) && use->cpu_active && queue && fence &&
            use->queue.Get()==queue && use->submission_fence.Get()==fence;
    }
    bool held(ID3D12Resource* resource)const{
        if(!resource)return false;
        for(auto* use=head;use;use=use->next)
            for(const auto& texture:use->textures)if(texture.Get()==resource)return true;
        return false;
    }
    std::size_t size()const{std::size_t count{};for(auto* use=head;use;use=use->next)++count;return count;}
    bool erase_unrecorded(Use* target){
        if(!contains(target) || target->cpu_active || target->recorded ||
            target->phase!=modern_eye_pair::UsePhase::Recording)return false;
        auto** link=&head;while(*link && *link!=target)link=&(*link)->next;
        if(!*link)return false;*link=target->next;delete target;return true;
    }
    std::size_t collect(){
        std::size_t count{};auto** link=&head;
        while(*link){auto* use=*link;
            const auto completed=use->phase==modern_eye_pair::UsePhase::Pending && use->fence
                ? use->fence->GetCompletedValue():UINT64_MAX;
            if(modern_eye_pair::retired(use->phase,use->fence_value,completed,use->cpu_active)){
                *link=use->next;delete use;++count;
            }else link=&use->next;
        }return count;
    }
    std::size_t discard_after_reset(ID3D12GraphicsCommandList* command,HRESULT original_result){
        if(!command || FAILED(original_result))return 0;
        std::size_t count{};auto** link=&head;
        while(*link){auto* use=*link;
            if(!use->cpu_active && use->recorded && use->recording.Get()==command &&
                use->phase==modern_eye_pair::UsePhase::Recording){
                *link=use->next;delete use;++count;
            }else link=&use->next;
        }return count;
    }
    bool before_execute(Use* use,ID3D12CommandQueue* actual_queue,UINT actual_count,
        ID3D12CommandList* const* actual_lists){
        if(!contains(use) || !use->cpu_active || !use->recorded || !use->recording || !use->allocator ||
            !use->queue || !use->fence || use->phase!=modern_eye_pair::UsePhase::Recording ||
            actual_queue!=use->queue.Get() || actual_count!=1 || !actual_lists ||
            actual_lists[0]!=use->recording.Get())return false;
        use->phase=modern_eye_pair::UsePhase::Executing;return true;
    }
    bool after_signal(Use* use,HRESULT original_result,std::uint64_t actual_value){
        if(!contains(use) || use->phase!=modern_eye_pair::UsePhase::Executing)return false;
        if(FAILED(original_result) || !modern_eye_pair::finite(actual_value))return false;
        use->fence_value=actual_value;use->phase=modern_eye_pair::UsePhase::Pending;return true;
    }
    void finish_cpu(Use* use){
        if(!contains(use))return;
        use->cpu_active=false;erase_unrecorded(use);collect();
    }
};
} // namespace w3vr::modern_eye_source_use
