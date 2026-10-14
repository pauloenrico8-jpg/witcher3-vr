#pragma once
#include <openxr/openxr.h>
namespace w3vr::session_events {
enum class Action {None,Begin,End,Terminal};
struct Decision {bool accepted{};Action action{};XrSessionState state{};};
inline bool known(XrSessionState state){return state>=XR_SESSION_STATE_IDLE && state<=XR_SESSION_STATE_EXITING;}
inline Decision decide(XrSession expected,const XrEventDataSessionStateChanged& event,bool running){
    if(expected==XR_NULL_HANDLE || event.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED ||
        event.session!=expected || !known(event.state))return {};
    auto action=Action::None;
    if(event.state==XR_SESSION_STATE_READY && !running)action=Action::Begin;
    else if(event.state==XR_SESSION_STATE_STOPPING && running)action=Action::End;
    else if(event.state==XR_SESSION_STATE_LOSS_PENDING || event.state==XR_SESSION_STATE_EXITING)action=Action::Terminal;
    return {true,action,event.state};
}
inline bool frames_allowed(bool running,XrSessionState state){
    return running && (state==XR_SESSION_STATE_READY || state==XR_SESSION_STATE_SYNCHRONIZED ||
        state==XR_SESSION_STATE_VISIBLE || state==XR_SESSION_STATE_FOCUSED);
}
inline bool visibility_event(XrSession expected,const XrEventDataVisibilityMaskChangedKHR& event){
    return expected!=XR_NULL_HANDLE && event.session==expected &&
        event.type==XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR &&
        event.viewConfigurationType==XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO && event.viewIndex<2;
}
inline bool after_end(bool was_running,XrResult result){return XR_SUCCEEDED(result)?false:was_running;}
// Pure policy only. Caller must stop/drain the frame loop before EndSession and
// serialize session/frame access. Matching an event is not a runtime lifetime.
} // namespace w3vr::session_events
