#pragma once
#include <cstdint>
namespace w3vr::xr_fence {
// Event wakeup is only a reason to inspect the SAME fence again.
// Caller retains the fence and event, and serializes its registrations.
inline bool completed(std::uint64_t observed,std::uint64_t target) noexcept {
    return target!=0 && target!=UINT64_MAX && observed!=UINT64_MAX && observed>=target;
}
template<class Read,class Register,class Wait>
bool wait_for_completion(std::uint64_t target,Read&& read,Register&& register_event,Wait&& wait) {
    if(target==UINT64_MAX)return false; // unknown submission, never retirement
    if(target==0)return true; // no submitted use of this allocator
    const auto before=read();
    if(before==UINT64_MAX)return false; // D3D12 device removed, not completion
    if(completed(before,target))return true;
    if(!register_event(target) || !wait())return false;
    return completed(read(),target); // unrelated/stale event cannot retire it
}
} // namespace w3vr::xr_fence
