#include "openxr_image_protocol.h"
#include <cstdlib>
#include <iostream>
#include <thread>
namespace x=w3vr::xr_image;
unsigned checks{};
void check(bool v,const char* why){++checks;if(!v){std::cerr<<why<<'\n';std::exit(1);}}
x::Input input(unsigned nonce=1){return {{0x1000,2,nonce},0x2000,0x3000,7,3};}
x::Ticket acquire_wait(x::Image& image,XrResult acquire=XR_SUCCESS,XrResult wait=XR_SUCCESS,uint32_t index=1){
    x::Ticket t;check(image.begin(input(),t),"begin");uint32_t out=99;
    check(image.acquire(t,out,[&](uint32_t& value){value=index;return acquire;})==acquire&&out==index,"acquire forwarding");
    check(image.wait(t,[&]{return wait;})==wait,"wait forwarding");return t;
}
int main(){
    {
        x::Image image;x::Ticket t,sentinel{input(),77};auto out=sentinel;
        auto i=input();i.images=0;check(!image.begin(i,out)&&out.nonce==77,"empty image count");
        i=input();i.frame.session=0;check(!image.begin(i,out),"missing session");
        check(image.begin(input(),t),"normal begin");uint32_t index=55;unsigned acquires{},waits{},releases{};
        check(!image.writable(t,1,true)&&!image.releasable(t),"acquire not availability");
        check(image.acquire(t,index,[&](uint32_t& v){++acquires;v=1;return XR_SUCCESS;})==XR_SUCCESS&&index==1,"acquire once");
        check(!image.writable(t,index,true)&&!image.releasable(t),"acquired image writable before wait");
        check(image.acquire(t,index,[](auto&)->XrResult{check(false,"duplicate acquire callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"duplicate acquire");
        check(image.wait(t,[&]{++waits;return XR_SUCCESS;})==XR_SUCCESS,"wait once");
        check(image.wait(t,[]()->XrResult{check(false,"duplicate wait callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"duplicate wait");
        check(image.writable(t,index,true)&&!image.writable(t,3,true)&&!image.writable(t,index,false),"index/layout");
        check(image.start_recording(t,index,true)&&!image.start_recording(t,index,true),"record once");
        check(image.transition(t,4,0x400)&&!image.color_ready(t),"copy destination state");
        check(!image.before_execute(t,0x3000,true,true),"copy dest sent to runtime");
        check(image.transition(t,0x400,4)&&image.color_ready(t),"color restored");
        check(!image.before_execute(t,0x3001,true,true)&&!image.before_execute(t,0x3000,false,true),"wrong queue/open list");
        check(image.before_execute(t,0x3000,true,true)&&!image.before_execute(t,0x3000,true,true),"execute claim");
        check(!image.releasable(t),"execute in flight released");
        check(image.execution_returned(t,0x3000,true,true)&&!image.releasable(t),"returned execute without signal");
        check(image.signal(t,0x3000,true,9)&&image.releasable(t),"exact queue signal");
        check(image.release(t,[&]{++releases;return XR_SUCCESS;})==XR_SUCCESS&&image.layer_allowed(t)&&!image.pending(),"release/layer");
        check(image.release(t,[]()->XrResult{check(false,"duplicate release callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"duplicate release");
        check(acquires==1&&waits==1&&releases==1,"callback counts");
        check(!image.begin(input(),out),"same XR frame replay");
        check(image.begin(input(2),out)&&!image.layer_allowed(t),"next frame/old ticket");
    }
    for(auto result:{XR_TIMEOUT_EXPIRED,XR_ERROR_RUNTIME_FAILURE,XR_EVENT_UNAVAILABLE}){
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,result);
        check(!image.writable(t,1,true)&&!image.releasable(t)&&image.pending(),"failed/timeout wait released or drew");
        check(image.release(t,[]()->XrResult{check(false,"illegal release after wait");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"unavailable release");
        x::Ticket next;check(!image.begin(input(2),next),"pending image reacquired");
    }
    for(unsigned loss=0;loss<2;++loss){
        x::Image image;auto t=acquire_wait(image,loss?XR_SUCCESS:XR_SESSION_LOSS_PENDING,loss?XR_SESSION_LOSS_PENDING:XR_SUCCESS);
        check(!image.writable(t,1,true)&&image.releasable(t),"loss cleanup");
        check(image.release(t,[]{return XR_SUCCESS;})==XR_SUCCESS&&!image.layer_allowed(t),"loss published layer");
        x::Ticket next;check(!image.begin(input(2),next),"lost session rearmed");
    }
    {
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_SUCCESS,99);
        check(!image.writable(t,99,true)&&image.releasable(t),"bad index dereferenced or cleanup blocked");
        check(image.release(t,[]{return XR_SUCCESS;})==XR_SUCCESS&&!image.layer_allowed(t),"unwritten cleanup published");
    }
    for(bool record:{false,true}){
        x::Image image;auto t=acquire_wait(image);
        if(record)check(image.start_recording(t,1,true),"nonexecuted recording");
        check(image.releasable(t)&&image.release(t,[]{return XR_SUCCESS;})==XR_SUCCESS&&!image.layer_allowed(t),"no execute cleanup");
    }
    for(auto result:{XR_ERROR_RUNTIME_FAILURE,XR_TIMEOUT_EXPIRED}){
        x::Image image;auto t=acquire_wait(image);check(image.release(t,[&]{return result;})==result,"release return changed");
        check(image.pending()&&!image.layer_allowed(t),"failed release published image");x::Ticket next;check(!image.begin(input(2),next),"failed release rearmed");
    }
    for(unsigned bad=0;bad<4;++bad){
        x::Image image;auto t=acquire_wait(image);check(image.start_recording(t,1,true)&&image.before_execute(t,0x3000,true,true),"signal setup");
        check(image.execution_returned(t,0x3000,true,true),"signal execute");
        check(!image.signal(t,bad==3?0x3001:0x3000,bad!=0,bad==1?0:bad==2?UINT64_MAX:9),"bad signal admitted");
        check(!image.releasable(t)&&image.pending(),"unknown GPU execution released");x::Ticket next;check(!image.begin(input(2),next),"bad signal rearmed");
    }
    {
        x::Image image;auto t=acquire_wait(image);check(image.start_recording(t,1,true),"bad state setup");
        check(!image.transition(t,0,4)&&!image.releasable(t),"fictional COMMON accepted");
    }
    for(unsigned changed=0;changed<4;++changed){
        x::Image image;auto t=acquire_wait(image);auto bad=t;
        if(changed==0)bad.input.frame.epoch++;if(changed==1)bad.input.chain++;if(changed==2)bad.input.queue++;if(changed==3)bad.nonce++;
        check(!image.writable(bad,1,true)&&!image.releasable(bad),"foreign ticket");
        check(image.release(bad,[]()->XrResult{check(false,"foreign callback");return XR_SUCCESS;})==XR_ERROR_CALL_ORDER_INVALID,"foreign release");
        check(image.releasable(t),"foreign call burned valid receipt");
    }
    {
        x::Image image;auto t=acquire_wait(image);bool rejected{};
        std::thread other([&]{rejected=!image.writable(t,1,true)&&!image.releasable(t);});other.join();
        check(rejected&&image.releasable(t),"cross-thread borrowed image receipt");
    }
    {
        x::Image image;x::Ticket t;check(image.begin(input(),t),"failed acquire setup");uint32_t index=17;
        check(image.acquire(t,index,[](uint32_t& out){out=999;return XR_ERROR_RUNTIME_FAILURE;})==XR_ERROR_RUNTIME_FAILURE&&index==999,"failed acquire output changed");
        check(!image.pending()&&!image.releasable(t),"failed acquire retained invented image");
    }
    std::cout<<checks<<" CPU checks of own image acquisition, wait, color transitions, exact queue signal, release, pending quarantine and stale callbacks. Handles/results/state receipts fabricated; no OpenXR runtime, SDK, Witcher, GPU or Quest executed.\n";
}
