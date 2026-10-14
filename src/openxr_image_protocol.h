#pragma once
#include "xr_frame_identity.h"
#include <openxr/openxr.h>
#include <cstdint>
#include <thread>
namespace w3vr::xr_image {
// Own numeric protocol. Host serializes this process-lifetime object through
// the managed frame lease. Does not retain runtime/COM/GPU/allocator lifetime.
struct Input {
    xr_frame::Identity frame{};std::uintptr_t chain{},queue{};
    std::uint32_t generation{},images{};
    bool operator==(const Input&)const=default;
};
struct Ticket {Input input{};std::uint64_t nonce{};};
inline bool succeeded(XrResult r){return r==XR_SUCCESS || r==XR_SESSION_LOSS_PENDING;}
class Image {
    Ticket active_{};xr_frame::Identity previous_{};std::uint64_t nonce_{};
    std::uintptr_t bound_session_{},bound_chain_{},bound_queue_{};
    std::thread::id thread_{};
    std::uint32_t index_{UINT32_MAX},state_{};std::uint64_t fence_value_{};
    bool acquire_called_{},acquired_{},wait_called_{},available_{},loss_{},
         recording_{},execute_called_{},executed_{},signalled_{},release_called_{},released_{},failed_{};
    bool exact(Ticket t)const{
        return t.nonce && t.nonce==active_.nonce && t.input==active_.input &&
            thread_==std::this_thread::get_id();
    }
public:
    Image()=default;Image(const Image&)=delete;Image& operator=(const Image&)=delete;
    bool begin(Input input,Ticket& out){
        if(failed_ || (active_.nonce && !released_) || !xr_frame::valid(input.frame) ||
            !input.chain || !input.queue || !input.generation || !input.images || nonce_>=UINT64_MAX-1 ||
            (xr_frame::valid(previous_) && !xr_frame::newer(input.frame,previous_)) ||
            (bound_session_ && (input.frame.session!=bound_session_ || input.chain!=bound_chain_ || input.queue!=bound_queue_)))return false;
        bound_session_=input.frame.session;bound_chain_=input.chain;bound_queue_=input.queue;
        previous_=input.frame;active_={input,++nonce_};thread_=std::this_thread::get_id();
        index_=UINT32_MAX;state_=0;fence_value_=0;
        acquire_called_=acquired_=wait_called_=available_=loss_=recording_=execute_called_=executed_=signalled_=release_called_=released_=false;
        out=active_;return true;
    }
    template<class Call> XrResult acquire(Ticket ticket,std::uint32_t& out,Call&& call){
        if(!exact(ticket) || acquire_called_ || failed_)return XR_ERROR_CALL_ORDER_INVALID;
        acquire_called_=true;
        const auto result=call(out); // forward the original output/result exactly once
        if(succeeded(result)){acquired_=true;index_=out;loss_=result==XR_SESSION_LOSS_PENDING;}
        else failed_=true;
        return result;
    }
    template<class Call> XrResult wait(Ticket ticket,Call&& call){
        if(!exact(ticket) || !acquired_ || wait_called_ || failed_)return XR_ERROR_CALL_ORDER_INVALID;
        wait_called_=true;const auto result=call();
        available_=succeeded(result);if(available_)state_=4;loss_=loss_ || result==XR_SESSION_LOSS_PENDING;
        if(!available_)failed_=true; // retain pending acquisition; never release or redraw by guessing
        return result;
    }
    bool writable(Ticket t,std::uint32_t index,bool layout_ok)const{
        return exact(t) && available_ && !failed_ && !loss_ && !release_called_ &&
            index==index_ && index<input_count(t) && layout_ok;
    }
    bool start_recording(Ticket t,std::uint32_t index,bool layout_ok){
        if(!writable(t,index,layout_ok) || recording_)return false;recording_=true;return true;
    }
    bool transition(Ticket t,std::uint32_t before,std::uint32_t after){
        if(!exact(t) || !recording_ || execute_called_ || failed_ || before!=state_ ||
            (after!=4 && after!=0x400)){if(exact(t))failed_=true;return false;}
        state_=after;return true;
    }
    bool color_ready(Ticket t)const{return exact(t) && available_ && !failed_ && state_==4;}
    bool before_execute(Ticket t,std::uintptr_t queue,bool closed,bool color_rt){
        if(!exact(t) || !recording_ || execute_called_ || failed_ || queue!=t.input.queue || !closed || !color_rt || state_!=4)return false;
        execute_called_=true;return true;
    }
    bool execution_returned(Ticket t,std::uintptr_t queue,bool closed,bool color_rt){
        if(!exact(t) || !recording_ || !execute_called_ || executed_ || failed_ || queue!=t.input.queue || !closed || !color_rt || state_!=4)return false;
        executed_=true;return true;
    }
    bool signal(Ticket t,std::uintptr_t queue,bool success,std::uint64_t value){
        if(!exact(t) || !executed_ || signalled_ || failed_ || queue!=t.input.queue ||
            !success || !value || value==UINT64_MAX){if(exact(t))failed_=true;return false;}
        signalled_=true;fence_value_=value;return true;
    }
    bool releasable(Ticket t)const{
        // No write: successful Wait already guarantees color RT + session queue.
        // Recorded write: exact closed recording submitted/restored on that queue.
        return exact(t) && available_ && !failed_ && !release_called_ &&
            (!execute_called_ || (executed_ && signalled_ && fence_value_));
    }
    template<class Call> XrResult release(Ticket t,Call&& call){
        if(!releasable(t))return XR_ERROR_CALL_ORDER_INVALID;
        release_called_=true;const auto result=call();
        released_=succeeded(result);loss_=loss_ || result==XR_SESSION_LOSS_PENDING;
        if(!released_ || loss_)failed_=true;return result;
    }
    bool layer_allowed(Ticket t)const{
        return exact(t) && released_ && !failed_ && !loss_ && recording_ && executed_ && signalled_;
    }
    void fail(Ticket t){if(exact(t))failed_=true;}
    bool pending()const{return active_.nonce && acquired_ && !released_;}
private:
    static std::uint32_t input_count(Ticket t){return t.input.images;}
};
} // namespace w3vr::xr_image
