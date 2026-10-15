#pragma once
#include "modern_eye_source_use.h"
#include "openxr_image_protocol.h"
#include "xr_allocator_submissions.h"

namespace w3vr::xr_submission {
// Caller MUST hold the source-store mutex and the exclusive managed frame lease.
// No callbacks, COM/Native calls or allocations occur between the read-only
// checks and marks. These exact stores/inputs cannot change in that interval.
// This coordinates OWN submission bookkeeping; it is not a game/all-call lease
// and does not call Execute, cancel a submitted use, or prove GPU completion.
inline bool begin(modern_eye_source_use::Store* sources,modern_eye_source_use::Use* use,
    xr_image::Image& image,xr_image::Ticket image_ticket,xr_image::Recording recording,
    xr_allocator::Store& allocator,const xr_allocator::Ticket& allocator_ticket,
    ID3D12CommandQueue* queue,UINT count,ID3D12CommandList* const* lists,bool closed){
    if(bool(sources)!=bool(use) || !queue || count!=1 || !lists || !lists[0] ||
        recording.command!=reinterpret_cast<std::uintptr_t>(lists[0]))return false;
    const auto queue_key=reinterpret_cast<std::uintptr_t>(queue);
    // Refusal of ANY participant must leave ALL participants unmarked.
    if((sources && !sources->can_execute(use,queue,count,lists)) ||
        !image.can_execute(image_ticket,recording,queue_key,closed) ||
        !allocator.can_execute(allocator_ticket,queue,count,lists))return false;
    // The predicates above and those used by these marks are the same. No
    // user code or unlock is interleaved; a successful mark cannot retire refs.
    const bool source_marked=!sources || sources->before_execute(use,queue,count,lists);
    const bool image_marked=image.before_execute(image_ticket,recording,queue_key,closed);
    const bool allocator_marked=allocator.before_execute(allocator_ticket,queue,count,lists);
    return source_marked && image_marked && allocator_marked;
}
} // namespace w3vr::xr_submission
