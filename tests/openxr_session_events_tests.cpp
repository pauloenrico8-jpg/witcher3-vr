#include "openxr_session_events.h"
#include <cstdlib>
#include <iostream>
namespace events=w3vr::session_events;
unsigned checks{};
void require(bool value,const char* why){++checks;if(!value){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    const auto session=reinterpret_cast<XrSession>(static_cast<uintptr_t>(1));
    XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};event.session=session;
    for(int raw=0;raw<=9;++raw)for(bool running:{false,true}){
        event.state=static_cast<XrSessionState>(raw);auto decision=events::decide(session,event,running);
        const bool known=raw>=1 && raw<=8;require(decision.accepted==known,"Unknown state accepted");
        if(known){
            auto expected=events::Action::None;
            if(raw==2 && !running)expected=events::Action::Begin;
            if(raw==6 && running)expected=events::Action::End;
            if(raw==7 || raw==8)expected=events::Action::Terminal;
            require(decision.action==expected && decision.state==event.state,"Session action does not match official order");
        }
        const bool may_frame=running && raw>=2 && raw<=5;
        require(events::frames_allowed(running,event.state)==may_frame,"Inactive/stopping/lost/exiting session admitted frame loop");
        auto foreign=event;foreign.session=reinterpret_cast<XrSession>(static_cast<uintptr_t>(2));
        require(!events::decide(session,foreign,running).accepted,"Foreign session altered current state");
        require(!events::decide(XR_NULL_HANDLE,event,running).accepted,"Null session admitted event");
    }
    event.state=XR_SESSION_STATE_READY;event.type=XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING;
    require(!events::decide(session,event,false).accepted,"Wrong event type admitted");
    for(const auto result:{XR_ERROR_SESSION_NOT_STOPPING,XR_ERROR_RUNTIME_FAILURE,XR_ERROR_SESSION_LOST}){
        require(events::after_end(true,result),"Failed runtime call pretended session stopped");
        require(!events::frames_allowed(events::after_end(true,result),XR_SESSION_STATE_STOPPING),"Failed stop resumed frames");
    }
    require(!events::after_end(true,XR_SUCCESS) && !events::after_end(true,XR_SESSION_LOSS_PENDING),"Successful end retained running claim");
    require(!events::after_end(false,XR_ERROR_RUNTIME_FAILURE),"Failed end invented running session");
    XrEventDataVisibilityMaskChangedKHR mask{XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR};
    mask.session=session;mask.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    for(uint32_t eye=0;eye<3;++eye){mask.viewIndex=eye;require(events::visibility_event(session,mask)==(eye<2),"Unexpected eye admitted mask refresh");}
    mask.viewIndex=0;
    require(!events::visibility_event(XR_NULL_HANDLE,mask),"Null mask session admitted");
    auto foreign_mask=mask;foreign_mask.session=reinterpret_cast<XrSession>(static_cast<uintptr_t>(2));
    require(!events::visibility_event(session,foreign_mask),"Foreign session changed headset masks");
    mask.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_MONO;
    require(!events::visibility_event(session,mask),"Foreign view configuration changed stereo masks");
    mask.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;mask.type=XR_TYPE_EVENT_DATA_BUFFER;
    require(!events::visibility_event(session,mask),"Wrong mask event type admitted");
    std::cout<<checks<<" CPU checks of owned session events and return values. No OpenXR runtime, SDK, Witcher, GPU or Quest executed.\n";
}
