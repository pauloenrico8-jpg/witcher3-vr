#include "openxr_image_protocol.h"
#include "openxr_frame_cycle.h"
#include <cstdlib>
#include <iostream>
#include <thread>
#include <stdexcept>
namespace x=w3vr::xr_image;
unsigned checks{};
x::Recording binding(unsigned index=1){return {0x5000,0x6000,0x6100,index};}
void check(bool v,const char* why){++checks;if(!v){std::cerr<<why<<'\n';std::exit(1);}}
x::Input input(unsigned nonce=1){return {{0x1000,2,nonce},0x2000,0x3000,7,3};}
x::Ticket acquire_wait(x::Image& image,XrResult acquire=XR_SUCCESS,XrResult wait=XR_SUCCESS,uint32_t index=1){
    x::Ticket t;check(image.begin(input(),t),"begin");uint32_t out=99;
    check(image.acquire(t,out,[&](uint32_t& value){value=index;return acquire;})==acquire&&out==index,"acquire forwarding");
    check(image.wait(t,[&]{return wait;})==wait,"wait forwarding");return t;
}
void timeout_recovery_tests(){
    {
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);
        unsigned waits{},releases{},admissions{};auto owner=input(2);owner.generation++;
        auto admitted=[&]{++admissions;return true;};
        auto timeout=[&]{++waits;return XR_TIMEOUT_EXPIRED;};
        auto release=[&]{++releases;return XR_SUCCESS;};
        check(image.retryable_pending(),"timeout not retryable");
        const auto first=image.recover_wait_timeout(owner,admitted,timeout,release);
        check(first.state==x::RecoveryState::Pending&&first.wait_result==XR_TIMEOUT_EXPIRED&&waits==1&&releases==0,"timeout reacquired/released image");
        check(image.pending()&&image.retryable_pending()&&!image.writable(t,1,true),"pending cleanup drew");
        unsigned nested{};auto newer=input(3);newer.generation++;
        const auto done=image.recover_wait_timeout(newer,admitted,[&]{
            ++waits;check(!image.retryable_pending(),"recovery reentry admission");
            auto again=image.recover_wait_timeout(input(4),[]{return true;},[&]{++nested;return XR_SUCCESS;},[&]{++nested;return XR_SUCCESS;});
            check(again.state==x::RecoveryState::Rejected&&nested==0,"nested timeout recovery");return XR_SUCCESS;
        },[&]{
            ++releases;check(!image.writable(t,1,true)&&!image.start_recording(t,binding(),true),"old recovered image drawn before release");return XR_SUCCESS;
        });
        check(done.state==x::RecoveryState::Released&&done.wait_result==XR_SUCCESS&&done.release_result==XR_SUCCESS,"legal pending recovery");
        check(waits==2&&releases==1&&admissions==3&&!image.pending()&&!image.layer_allowed(t),"recovery counts/published old image");
        x::Ticket next;check(!image.begin(input(2),next)&&!image.begin(input(3),next)&&image.begin(input(4),next),"cleanup frame reused for fresh acquire");
        uint32_t index{};check(image.acquire(next,index,[](auto& value){value=0;return XR_SUCCESS;})==XR_SUCCESS&&
            image.wait(next,[]{return XR_SUCCESS;})==XR_SUCCESS&&image.writable(next,0,true),"new frame remained cleanup-only");
    }
    for(unsigned changed=0;changed<8;++changed){
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);auto owner=input(2);
        if(changed==0)owner.frame.session++;
        if(changed==1)owner.frame.epoch++;
        if(changed==2)owner.frame.nonce=1;
        if(changed==3)owner.chain++;
        if(changed==4)owner.queue++;
        if(changed==5)owner.images++;
        if(changed==6)owner.generation=0;
        if(changed==7)owner.frame.nonce=0;
        unsigned calls{};const auto result=image.recover_wait_timeout(owner,[]{return true;},[&]{++calls;return XR_SUCCESS;},[&]{++calls;return XR_SUCCESS;});
        check(result.state==x::RecoveryState::Rejected&&calls==0&&image.retryable_pending(),"foreign cleanup owner burned pending image");
        auto good=image.recover_wait_timeout(input(2),[]{return true;},[]{return XR_SUCCESS;},[]{return XR_SUCCESS;});
        check(good.state==x::RecoveryState::Released&&!image.layer_allowed(t),"legitimate cleanup after foreign rejection");
    }
    {
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);unsigned waits{},releases{};
        const auto rejected=image.recover_wait_timeout(input(2),[]{return false;},[&]{++waits;return XR_SUCCESS;},[&]{++releases;return XR_SUCCESS;});
        check(rejected.state==x::RecoveryState::Rejected&&waits==0&&releases==0&&image.retryable_pending(),"closed admission called runtime");
        unsigned seen{};auto result=image.recover_wait_timeout(input(2),[&]{return ++seen==1;},[&]{++waits;return XR_SUCCESS;},[&]{++releases;return XR_SUCCESS;});
        check(result.state==x::RecoveryState::Failed&&waits==1&&releases==0&&image.pending()&&!image.retryable_pending(),"changed owner released image");
        check(!image.writable(t,1,true),"changed owner drew image");
    }
    for(XrResult wait:{XR_ERROR_RUNTIME_FAILURE,XR_ERROR_SESSION_LOST,XR_EVENT_UNAVAILABLE}){
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);unsigned releases{};
        const auto result=image.recover_wait_timeout(input(2),[]{return true;},[&]{return wait;},[&]{++releases;return XR_SUCCESS;});
        check(result.state==x::RecoveryState::Failed&&result.wait_result==wait&&releases==0&&image.pending()&&!image.retryable_pending(),"fatal wait converted to timeout");
        check(!image.layer_allowed(t)&&!image.writable(t,1,true),"fatal wait published");
    }
    for(XrResult release:{XR_ERROR_RUNTIME_FAILURE,XR_TIMEOUT_EXPIRED,XR_SESSION_LOSS_PENDING}){
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);
        const auto result=image.recover_wait_timeout(input(2),[]{return true;},[]{return XR_SUCCESS;},[&]{return release;});
        check(result.state==x::RecoveryState::Failed&&result.release_result==release&&!image.layer_allowed(t)&&!image.retryable_pending(),"failed/lost release recovered silently");
        check(image.pending()==(release!=XR_SESSION_LOSS_PENDING),"failed release lost acquired state");
    }
    {
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);unsigned releases{};
        const auto result=image.recover_wait_timeout(input(2),[]{return true;},[]{return XR_SESSION_LOSS_PENDING;},[&]{++releases;return XR_SUCCESS;});
        check(result.state==x::RecoveryState::Failed&&releases==1&&!image.pending()&&!image.layer_allowed(t),"loss cleanup or layer policy");
        x::Ticket next;check(!image.begin(input(3),next),"lost session rearmed after cleanup");
    }
    {
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);unsigned calls{};bool rejected{};
        std::thread other([&]{rejected=image.recover_wait_timeout(input(2),[]{return true;},[&]{++calls;return XR_SUCCESS;},[&]{++calls;return XR_SUCCESS;}).state==x::RecoveryState::Rejected;});other.join();
        check(rejected&&calls==0&&image.retryable_pending(),"other thread recovered borrowed image");
        check(image.wait(t,[]{return XR_SUCCESS;})==XR_SUCCESS&&image.writable(t,1,true),"same frame legal wait retry");
    }
    for(unsigned fault=0;fault<2;++fault){
        x::Image image;auto t=acquire_wait(image,XR_SUCCESS,XR_TIMEOUT_EXPIRED);bool caught{};unsigned releases{};
        try {image.recover_wait_timeout(input(2),[]{return true;},[&]()->XrResult{if(fault==0)throw std::runtime_error("own wait fault");return XR_SUCCESS;},[&]()->XrResult{++releases;throw std::runtime_error("own release fault");});}
        catch(const std::runtime_error&){caught=true;}
        check(caught&&releases==fault&&image.pending()&&!image.retryable_pending()&&!image.writable(t,1,true),"exception rearmed cleanup");
    }
    {
        x::Image image;auto t=acquire_wait(image);check(image.start_recording(t,binding(),true)&&image.before_execute(t,binding(),0x3000,true),"executed failure setup");
        check(image.execution_returned(t,binding(),0x3000,true)&&!image.signal(t,0x3000,false,8),"executed failure fixture");
        unsigned calls{};auto result=image.recover_wait_timeout(input(2),[]{return true;},[&]{++calls;return XR_SUCCESS;},[&]{++calls;return XR_SUCCESS;});
        check(result.state==x::RecoveryState::Rejected&&calls==0&&image.pending(),"unknown GPU submission used timeout recovery");
    }
}
void frame_cleanup_sequence(){
    namespace f=w3vr::xr_frame_cycle;
    f::Cycle cycle;x::Image image;const auto session=reinterpret_cast<XrSession>(static_cast<uintptr_t>(0x1000));
    check(cycle.session_started(session),"cleanup frame session");unsigned acquisitions{},waits{},releases{},ends{};x::Ticket original;
    for(unsigned frame=1;frame<=4;++frame){
        {
            auto lease=cycle.prepare(session,7);check(bool(lease),"cleanup frame prepare");
            check(cycle.wait(lease,[]{return XR_SUCCESS;})==XR_SUCCESS&&cycle.begin(lease,[]{return XR_SUCCESS;})==XR_SUCCESS,"cleanup frame wait/begin");
            f::Payload payload;payload.state.predictedDisplayTime=100+frame*10;payload.state.predictedDisplayPeriod=10;payload.state.shouldRender=XR_TRUE;
            check(cycle.publish(lease,payload),"cleanup frame payload");
        }
        f::Payload payload;auto lease=cycle.present(session,payload);check(bool(lease)&&cycle.current(payload.ticket),"cleanup present lease");
        x::Input owner{payload.ticket.identity(),0x2000,0x3000,7,3};
        if(frame==1 || frame==4){
            x::Ticket t;check(image.begin(owner,t),"fresh image on acquisition frame");if(frame==1)original=t;
            uint32_t index{};check(image.acquire(t,index,[&](auto& v){++acquisitions;v=1;return XR_SUCCESS;})==XR_SUCCESS,"fresh acquisition callback");
            const auto waited=image.wait(t,[&]{++waits;return frame==1?XR_TIMEOUT_EXPIRED:XR_SUCCESS;});
            check(waited==(frame==1?XR_TIMEOUT_EXPIRED:XR_SUCCESS)&&image.writable(t,1,true)==(frame==4),"fresh image availability");
        }else{
            check(!cycle.current(original.input.frame,7),"old XR frame still current");
            auto recovered=image.recover_wait_timeout(owner,[&]{return cycle.current(payload.ticket);},[&]{
                ++waits;f::Payload another;check(!cycle.present(session,another)&&!cycle.prepare(session,7),"recovery overlapped another frame");
                return frame==2?XR_TIMEOUT_EXPIRED:XR_SUCCESS;
            },[&]{++releases;return XR_SUCCESS;});
            check(recovered.state==(frame==2?x::RecoveryState::Pending:x::RecoveryState::Released),"cleanup across frame boundaries");
            check(!image.layer_allowed(original)&&!image.writable(original,1,true),"old image displayed on new frame");
        }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=payload.state.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        check(cycle.end(lease,[&]{++ends;check(end.layerCount==0&&end.layers==nullptr,"cleanup sent layer");return XR_SUCCESS;})==XR_SUCCESS,"cleanup frame ended");
    }
    check(acquisitions==2&&waits==4&&releases==1&&ends==4,"cleanup acquired replacements or skipped frame end");
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
        check(image.start_recording(t,binding(index),true)&&!image.start_recording(t,binding(index),true),"record once");
        check(image.transition(t,binding(),4,0x400)&&!image.color_ready(t,binding()),"copy destination state");
        check(!image.before_execute(t,binding(),0x3000,true),"copy dest sent to runtime");
        check(image.transition(t,binding(),0x400,4)&&image.color_ready(t,binding()),"color restored");
        check(!image.before_execute(t,binding(),0x3001,true)&&!image.before_execute(t,binding(),0x3000,false),"wrong queue/open list");
        check(image.before_execute(t,binding(),0x3000,true)&&!image.before_execute(t,binding(),0x3000,true),"execute claim");
        check(!image.releasable(t),"execute in flight released");
        check(image.execution_returned(t,binding(),0x3000,true)&&!image.releasable(t),"returned execute without signal");
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
        if(record)check(image.start_recording(t,binding(1),true),"nonexecuted recording");
        check(image.releasable(t)&&image.release(t,[]{return XR_SUCCESS;})==XR_SUCCESS&&!image.layer_allowed(t),"no execute cleanup");
    }
    for(auto result:{XR_ERROR_RUNTIME_FAILURE,XR_TIMEOUT_EXPIRED}){
        x::Image image;auto t=acquire_wait(image);check(image.release(t,[&]{return result;})==result,"release return changed");
        check(image.pending()&&!image.layer_allowed(t),"failed release published image");x::Ticket next;check(!image.begin(input(2),next),"failed release rearmed");
    }
    for(unsigned bad=0;bad<4;++bad){
        x::Image image;auto t=acquire_wait(image);check(image.start_recording(t,binding(1),true)&&image.before_execute(t,binding(),0x3000,true),"signal setup");
        check(image.execution_returned(t,binding(),0x3000,true),"signal execute");
        check(!image.signal(t,bad==3?0x3001:0x3000,bad!=0,bad==1?0:bad==2?UINT64_MAX:9),"bad signal admitted");
        check(!image.releasable(t)&&image.pending(),"unknown GPU execution released");x::Ticket next;check(!image.begin(input(2),next),"bad signal rearmed");
    }
    {
        x::Image image;auto t=acquire_wait(image);check(image.start_recording(t,binding(1),true),"bad state setup");
        check(!image.transition(t,binding(),0,4)&&!image.releasable(t),"fictional COMMON accepted");
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
    for(unsigned changed=0;changed<4;++changed){
        x::Image image;auto t=acquire_wait(image);auto good=binding(),bad=good;
        if(changed==0)bad.command++;if(changed==1)bad.resource++;
        if(changed==2)bad.resource_identity++;if(changed==3)bad.index++;
        check(image.start_recording(t,good,true),"mixed recording setup");
        unsigned calls{};auto call=[&]{++calls;};
        check(!image.copy_source(t,bad,0x7100,call,call,call)&&calls==0,"mixed helper callbacks ran");
        check(!image.transition(t,bad,4,0x800)&&image.color_ready(t,good),"foreign transition burned actual recording");
        check(!image.before_execute(t,bad,0x3000,true)&&image.recording_matches(t,good),"foreign list executed");
        check(image.copy_source(t,good,0x7100,call,call,call)&&calls==3,"exact helper after refused foreign callback");
        check(image.color_ready(t,good),"helper did not restore color RT");
    }
    for(unsigned missing=0;missing<3;++missing){
        x::Image image;auto t=acquire_wait(image);auto bad=binding();
        if(missing==0)bad.command=0;if(missing==1)bad.resource=0;if(missing==2)bad.resource_identity=0;
        check(!image.start_recording(t,bad,true)&&image.start_recording(t,binding(),true),"unknown recording got admission");
    }
    {
        x::Image image;auto t=acquire_wait(image);const auto b=binding();unsigned calls{};
        check(image.start_recording(t,b,true),"source copy setup");auto call=[&]{++calls;};
        check(!image.copy_source(t,b,0,call,call,call)&&calls==0,"unknown destination copied");
        check(!image.copy_source(t,b,b.resource_identity,call,call,call)&&calls==0,"canonical alias copied");
        auto wrong=t;wrong.input.frame.nonce++;
        check(!image.copy_source(wrong,b,0x7100,call,call,call)&&calls==0,"stale helper ran");
        check(image.copy_source(t,b,0x7100,[&]{++calls;check(!image.color_ready(t,b)&&
            !image.before_execute(t,b,0x3000,true)&&!image.releasable(t),"copy-source submitted or released early");},call,call)&&calls==3,"copy helper order");
        check(image.before_execute(t,b,0x3000,true),"execute restored helper");
        check(!image.copy_source(t,b,0x7100,call,call,call)&&calls==3,"helper recorded after execute");
    }
    for(unsigned failure=0;failure<3;++failure){
        x::Image image;auto t=acquire_wait(image);const auto b=binding();unsigned calls{};
        check(image.start_recording(t,b,true),"exception setup");
        auto part=[&]{if(calls++==failure)throw std::runtime_error("own callback fault");};bool caught{};
        try{image.copy_source(t,b,0x7100,part,part,part);}catch(const std::runtime_error&){caught=true;}
        check(caught&&calls==failure+1&&!image.color_ready(t,b)&&!image.before_execute(t,b,0x3000,true),"partial copy restored readiness");
        check(!image.releasable(t)&&image.pending(),"partial exception returned runtime image");
    }
    timeout_recovery_tests();
    frame_cleanup_sequence();
    std::cout<<checks<<" CPU checks of own image acquisition, wait, color transitions, exact queue signal, release, timeout-only cleanup, pending quarantine and stale callbacks. Handles/results/state receipts fabricated; no OpenXR runtime, SDK, Witcher, GPU or Quest executed.\n";
}
