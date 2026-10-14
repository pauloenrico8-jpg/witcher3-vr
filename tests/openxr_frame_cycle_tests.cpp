#include "openxr_frame_cycle.h"
#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
namespace xr=w3vr::xr_frame_cycle;
std::atomic<unsigned> checks{};
void check(bool v,const char* why){++checks;if(!v){std::cerr<<why<<'\n';std::exit(1);}}
XrSession session(unsigned v=1){return reinterpret_cast<XrSession>(static_cast<uintptr_t>(v));}
xr::Payload payload(XrTime time=100){
    xr::Payload p;p.state.predictedDisplayTime=time;p.state.predictedDisplayPeriod=10;p.state.shouldRender=XR_TRUE;
    p.views_valid=true;p.flags=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
    p.views[0].pose.position.x=-0.03f;p.views[1].pose.position.x=0.03f;return p;
}
xr::Ticket prepare(xr::Cycle& c,xr::Payload p=payload(),XrResult br=XR_SUCCESS){
    auto l=c.prepare(session(),7);check(bool(l),"prepare");
    check(c.wait(l,[&]{xr::Payload out;check(!c.snapshot(session(),out),"early snapshot");return XR_SUCCESS;})==XR_SUCCESS,"wait");
    check(c.wait(l,[]{check(false,"repeat wait callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"repeat wait");
    check(c.begin(l,[&]{return br;})==br,"begin");
    check(c.begin(l,[]{check(false,"repeat begin callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"repeat begin");
    check(c.publish(l,p),"publish");return l.ticket();
}
void stop(xr::Cycle& c){
    c.request_stop(session());auto e=c.claim_stop(session());check(e!=0,"drain");
    check(c.stop(session(),e,[]{return XR_SUCCESS;})==XR_SUCCESS,"stop");
    check(c.stop(session(),e,[]{check(false,"repeat stop callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"repeat stop");
}
int main(){
    static_assert(!std::is_copy_constructible_v<xr::Cycle::Lease>);
    {
        xr::Cycle c;xr::Payload out=payload(500);
        check(!c.snapshot(session(),out)&&out.state.predictedDisplayTime==500,"rejected output");
        check(!c.session_started(XR_NULL_HANDLE)&&c.session_started(session()),"initial session");
        check(!c.session_started(session())&&!c.prepare(session(2),7)&&!c.prepare(session(),0),"unknown/repeated identity");
        auto t=prepare(c);check(c.snapshot(session(),out)&&out.ticket==t,"snapshot");
        out.views[0].pose.position.x=20;xr::Payload copy;check(c.snapshot(session(),copy)&&copy.views[0].pose.position.x==-0.03f,"copied views");
        check(!c.prepare(session(),7),"ready overwritten");
        {auto l=c.present(session(),copy);check(bool(l)&&copy.ticket==t,"exact present");
            check(!c.present(session(),out)&&!c.prepare(session(),7),"overlapping prepare/present");
            auto wrong=t;++wrong.epoch;check(!c.current(wrong)&&c.current(t),"epoch");
            check(c.end(l,[&]{check(c.current(t),"callback state mutex");return XR_SUCCESS;})==XR_SUCCESS,"end");
            check(c.end(l,[]{check(false,"repeat end callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"repeat end");}
        check(!c.current(t),"old ticket");
        auto next=prepare(c,payload(110));check(next.nonce>t.nonce,"nonce");
        stop(c);check(c.session_started(session()),"restart");
        auto restarted=prepare(c,payload(100));check(restarted.epoch>next.epoch&&restarted.nonce>next.nonce&&!c.current(next),"restart ABA");stop(c);
    }
    {
        xr::Cycle c;check(c.session_started(session()),"concurrent start");
        std::promise<void> entered,release;auto future=release.get_future().share();
        std::atomic<unsigned> waits{},begins{},stops{};
        std::thread worker([&]{auto l=c.prepare(session(),7);check(bool(l),"worker lease");
            check(c.wait(l,[&]{++waits;entered.set_value();future.wait();return XR_SUCCESS;})==XR_SUCCESS,"blocked wait");
            check(c.begin(l,[&]{++begins;return XR_SUCCESS;})==XR_SUCCESS,"finish wait during stop");
            check(!c.publish(l,payload()),"stopping publication");});
        entered.get_future().wait();xr::Payload out=payload(999);
        check(!c.prepare(session(),7)&&!c.present(session(),out)&&out.state.predictedDisplayTime==999,"parallel callers");
        c.request_stop(session());check(!c.claim_stop(session()),"stop during wait");
        release.set_value();worker.join();auto e=c.claim_stop(session());check(e!=0,"worker drained");
        check(c.stop(session(),e,[&]{++stops;return XR_SUCCESS;})==XR_SUCCESS,"stop callback");
        check(waits==1&&begins==1&&stops==1,"exact call counts");
    }
    {
        xr::Cycle c;check(c.session_started(session()),"present start");auto t=prepare(c);
        std::promise<void> entered,release;auto f=release.get_future().share();
        std::thread worker([&]{xr::Payload out;auto l=c.present(session(),out);check(bool(l),"present lease");entered.set_value();f.wait();
            check(!c.current(t),"stopping layers");check(c.end(l,[]{return XR_SUCCESS;})==XR_SUCCESS,"finish present");});
        entered.get_future().wait();c.request_stop(session());check(!c.claim_stop(session()),"stop during present");
        release.set_value();worker.join();auto e=c.claim_stop(session());check(e!=0,"present drained");
        check(c.stop(session(),e,[]{return XR_ERROR_RUNTIME_FAILURE;})==XR_ERROR_RUNTIME_FAILURE,"failed stop");
        check(!c.claim_stop(session())&&!c.session_started(session())&&!c.prepare(session(),7),"stop rearmed");
    }
    for(auto failure:{XR_ERROR_RUNTIME_FAILURE,XR_SESSION_LOSS_PENDING})for(unsigned stage=0;stage<3;++stage){
        xr::Cycle c;check(c.session_started(session()),"failure start");
        {auto l=c.prepare(session(),7);auto w=c.wait(l,[&]{return stage==0?failure:XR_SUCCESS;});check(w==(stage==0?failure:XR_SUCCESS),"wait result");
            if(stage>0){auto b=c.begin(l,[&]{return stage==1?failure:XR_SUCCESS;});check(b==(stage==1?failure:XR_SUCCESS),"begin result");
                if(stage==2)check(c.publish(l,payload()),"failure publish");}}
        if(stage==2){xr::Payload out;auto l=c.present(session(),out);check(bool(l),"failure present");check(c.end(l,[&]{return failure;})==failure,"end result");}
        check(!c.prepare(session(),7),"failed call rearmed");stop(c);
    }
    for(unsigned stage=0;stage<4;++stage){
        xr::Cycle c;check(c.session_started(session()),"exception start");
        try{{auto l=c.prepare(session(),7);
            c.wait(l,[&]()->XrResult{if(stage==0)throw std::runtime_error("wait");return XR_SUCCESS;});
            c.begin(l,[&]()->XrResult{if(stage==1)throw std::runtime_error("begin");return XR_SUCCESS;});
            if(stage==2)throw std::runtime_error("locate");check(c.publish(l,payload()),"exception publish");}
            xr::Payload out;auto l=c.present(session(),out);c.end(l,[]()->XrResult{throw std::runtime_error("end");});
        }catch(const std::runtime_error&){}
        check(!c.prepare(session(),7),"exception rearmed");stop(c);
    }
    for(unsigned empty=0;empty<4;++empty){
        xr::Cycle c;check(c.session_started(session()),"empty start");auto p=payload();
        if(empty==0)p.state.shouldRender=XR_FALSE;if(empty==1)p.views_valid=false;if(empty==3)p.flags=XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        prepare(c,p,empty==2?XR_FRAME_DISCARDED:XR_SUCCESS);
        xr::Payload out;auto l=c.present(session(),out);check(bool(l)&&!out.renderable(),"empty/discarded layers");
        check(c.end(l,[]{return XR_SUCCESS;})==XR_SUCCESS,"empty end");
    }
    for(unsigned bad=0;bad<6;++bad){
        xr::Cycle c;check(c.session_started(session()),"malformed start");
        {auto l=c.prepare(session(),7);c.wait(l,[]{return XR_SUCCESS;});c.begin(l,[]{return XR_SUCCESS;});auto p=payload();
            if(bad==0)p.state.next=&p;if(bad==1)p.views[1].next=&p;if(bad==2)p.state.predictedDisplayTime=0;
            if(bad==3)p.state.predictedDisplayPeriod=0;if(bad==4)p.state.shouldRender=2;if(bad==5)p.views[0].type=XR_TYPE_UNKNOWN;
            check(!c.publish(l,p),"malformed payload");}
        check(!c.prepare(session(),7),"malformed rearmed");stop(c);
    }
    {
        xr::Cycle c;check(c.session_started(session()),"thread start");auto l=c.prepare(session(),7);
        std::thread other([&]{check(c.wait(l,[]{check(false,"foreign callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"foreign lease");});
        other.join();check(c.wait(l,[]{return XR_SUCCESS;})==XR_SUCCESS,"owner wait");
        check(c.begin(l,[]{return XR_SUCCESS;})==XR_SUCCESS&&c.publish(l,payload()),"owner finish");
    }
    {
        xr::Cycle c;check(c.session_started(session()),"unrelated stop start");c.request_stop(session(2));
        auto first=prepare(c);xr::Payload out;
        {auto l=c.present(session(),out);check(c.end(l,[]{return XR_SUCCESS;})==XR_SUCCESS,"first monotonic end");}
        {auto l=c.prepare(session(),7);check(bool(l),"monotonic prepare");
            check(c.begin(l,[]{check(false,"begin without wait callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"begin before wait");
            check(c.wait(l,[]{return XR_SUCCESS;})==XR_SUCCESS&&c.begin(l,[]{return XR_SUCCESS;})==XR_SUCCESS,"monotonic wait/begin");
            check(!c.publish(l,payload(100)),"non-increasing time admitted");}
        check(!c.prepare(session(),7)&&!c.current(first),"time failure rearmed");stop(c);
    }
    std::cout<<checks.load()<<" CPU checks of frame-cycle order, immutable copies, exceptions, concurrent callbacks and nonblocking stop. Runtime results/handles simulated; no OpenXR, SDK, Witcher, GPU or Quest executed.\n";
}
