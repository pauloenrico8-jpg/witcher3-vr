#pragma once
#include "modern_eye_source_use.h"
#include <array>
#include <cstddef>

namespace w3vr::modern_eye_resource_set {
// Each address owns its resource reference. Caller holds the capture mutex
// throughout replacement; this is not a Native/loader/all-call barrier.
struct Slot {
    ID3D12Resource** owner{};
    ID3D12Fence* fence{};
    std::uint64_t value{};
    bool recording_pending{};
};
inline bool retired(const Slot& slot,const modern_eye_source_use::Store& uses,bool protected_mode){
    if(!slot.owner)return false;
    if(uses.held(*slot.owner))return false;
    if(!protected_mode)return true; // Legacy route keeps its existing timeline policy.
    if(slot.recording_pending)return false;
    if(slot.value==0)return true; // Fresh or explicitly discarded own recording.
    if(!slot.fence || !modern_eye_pair::finite(slot.value))return false;
    const auto complete=slot.fence->GetCompletedValue();
    return complete!=UINT64_MAX && complete>=slot.value;
}
// Factory creates the same real D3D12 resources as the host. A failed create
// releases only private staging references, never a live old set or its state.
// Metadata is reset by the caller only after this returns true.
template<std::size_t N,class Factory>
bool replace(const std::array<Slot,N>& slots,modern_eye_source_use::Store& uses,
    bool protected_mode,Factory&& create){
    static_assert(N>0);
    uses.collect();
    std::array<ID3D12Resource*,N> previous{};
    for(std::size_t i=0;i<N;++i){
        if(!retired(slots[i],uses,protected_mode))return false;
        previous[i]=*slots[i].owner;
        for(std::size_t j=0;j<i;++j)
            if(slots[i].owner==slots[j].owner || (previous[i] && previous[i]==previous[j]))return false;
    }
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,N> staged;
    for(std::size_t i=0;i<N;++i){
        if(FAILED(create(i,staged[i].GetAddressOf())) || !staged[i])return false;
        for(std::size_t j=0;j<i;++j)if(staged[i].Get()==staged[j].Get())return false;
        for(auto* old:previous)if(staged[i].Get()==old)return false;
    }
    // A reentrant factory cannot commit over a changed owning address. The
    // caller's lock, not copied booleans or numeric IDs, supplies serialization.
    for(std::size_t i=0;i<N;++i)
        if(*slots[i].owner!=previous[i] || !retired(slots[i],uses,protected_mode))return false;
    for(std::size_t i=0;i<N;++i)*slots[i].owner=staged[i].Detach();
    for(auto* old:previous)if(old)old->Release();
    return true;
}
} // namespace w3vr::modern_eye_resource_set
