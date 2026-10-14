#pragma once
#include "xr_frame_identity.h"
#include <openxr/openxr.h>
#include <cstdint>
#include <thread>
#include <utility>
namespace w3vr::xr_image {
// Own numeric protocol. Host serializes this process-lifetime object through
// the managed frame lease. Does not retain runtime/COM/GPU/allocator lifetime.
struct Input {
    xr_frame::Identity frame{};std::uintptr_t chain{},queue{};
    std::uint32_t generation{},images{};
    bool operator==(const Input&)const=default;
};
struct Ticket {Input input{};std::uint64_t nonce{};};
// Borrowed numeric identity of one recording, not a COM/GPU/runtime lease.
struct Recording {
    std::uintptr_t command{},resource{},resource_identity{};std::uint32_t index{};
    bool operator==(const Recording&)const=default;
};
enum class RecoveryState { Rejected,Pending,Released,Failed };
struct Recovery {
    RecoveryState state{RecoveryState::Rejected};
    XrResult wait_result{XR_ERROR_CALL_ORDER_INVALID},release_result{XR_ERROR_CALL_ORDER_INVALID};
};
inline bool succeeded(XrResult r){return r==XR_SUCCESS || r==XR_SESSION_LOSS_PENDING;}
class Image {
    Ticket active_{};Recording binding_{};xr_frame::Identity previous_{};std::uint64_t nonce_{};
    std::uintptr_t bound_session_{},bound_chain_{},bound_queue_{};
    std::thread::id thread_{};
    std::uint32_t index_{UINT32_MAX},state_{};std::uint64_t fence_value_{};
    bool acquire_called_{},acquired_{},wait_called_{},available_{},loss_{},
         wait_active_{},timed_out_{},recovery_active_{},cleanup_only_{},recording_{},copy_active_{},execute_called_{},executed_{},signalled_{},release_called_{},released_{},failed_{};
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
        index_=UINT32_MAX;state_=0;fence_value_=0;binding_={};
        acquire_called_=acquired_=wait_called_=available_=loss_=wait_active_=timed_out_=recovery_active_=cleanup_only_=recording_=copy_active_=execute_called_=executed_=signalled_=release_called_=released_=false;
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
        if(!exact(ticket) || !acquired_ || available_ || wait_active_ ||
           (wait_called_ && !timed_out_) || failed_)return XR_ERROR_CALL_ORDER_INVALID;
        wait_called_=wait_active_=true;timed_out_=false;
        XrResult result;
        try {result=call();}
        catch(...) {wait_active_=false;failed_=true;throw;}
        wait_active_=false;
        timed_out_=result==XR_TIMEOUT_EXPIRED;
        available_=succeeded(result);if(available_)state_=4;
        loss_=loss_ || result==XR_SESSION_LOSS_PENDING;
        if(!available_ && !timed_out_)failed_=true;
        return result; // Timeout keeps the same acquisition; it is not a fatal error.
    }
    bool retryable_pending()const{
        return pending() && thread_==std::this_thread::get_id() && !failed_ &&
            timed_out_ && !wait_active_ && !recovery_active_ && !available_ &&
            !recording_ && !execute_called_ && !release_called_;
    }
    // Cleanup only, in a later frame of the same active session/epoch/chain/queue.
    // No old image can be drawn or published. Generation changes do not change
    // the untouched runtime acquisition, but handles and image extent must match.
    template<class Admitted,class Wait,class Release>
    Recovery recover_wait_timeout(Input current,Admitted&& admitted,Wait&& wait_call,Release&& release_call){
        if(!retryable_pending() || !xr_frame::valid(current.frame) || !current.generation ||
            current.frame.session!=active_.input.frame.session || current.frame.epoch!=active_.input.frame.epoch ||
            !xr_frame::newer(current.frame,active_.input.frame) ||
            (current.frame!=previous_ && !xr_frame::newer(current.frame,previous_)) ||
            current.chain!=active_.input.chain || current.queue!=active_.input.queue ||
            current.images!=active_.input.images || !admitted())return {};
        cleanup_only_=true;
        previous_=current.frame; // This cleanup frame is consumed even if the wait times out again.
        recovery_active_=true;
        Recovery result;
        try {
            result.wait_result=wait(active_,std::forward<Wait>(wait_call));
            if(result.wait_result==XR_TIMEOUT_EXPIRED)result.state=RecoveryState::Pending;
            else if(!succeeded(result.wait_result))result.state=RecoveryState::Failed;
            else if(!admitted()){failed_=true;result.state=RecoveryState::Failed;}
            else {
                result.release_result=release(active_,std::forward<Release>(release_call));
                result.state=succeeded(result.release_result)&&!failed_&&!loss_
                    ? RecoveryState::Released:RecoveryState::Failed;
            }
        } catch(...) {recovery_active_=false;failed_=true;throw;}
        recovery_active_=false;return result;
    }
    bool writable(Ticket t,std::uint32_t index,bool layout_ok)const{
        return exact(t) && available_ && !failed_ && !loss_ && !cleanup_only_ && !release_called_ &&
            index==index_ && index<input_count(t) && layout_ok;
    }
    bool start_recording(Ticket t,Recording binding,bool layout_ok){
        if(!writable(t,binding.index,layout_ok) || recording_ || !binding.command ||
            !binding.resource || !binding.resource_identity)return false;
        binding_=binding;recording_=true;return true;
    }
    bool recording_matches(Ticket t,Recording binding)const {
        return exact(t) && recording_ && !failed_ && !release_called_ && binding==binding_;
    }
    bool transition(Ticket t,Recording binding,std::uint32_t before,std::uint32_t after){
        if(!recording_matches(t,binding))return false; // a foreign callback cannot revoke this recording
        if(copy_active_ || execute_called_ || before!=state_ || (after!=4 && after!=0x400 && after!=0x800)){
            failed_=true;return false;
        }
        state_=after;return true;
    }
    bool color_ready(Ticket t,Recording binding)const {
        return recording_matches(t,binding) && available_ && !copy_active_ && state_==4;
    }
    template<class Before,class Copy,class After>
    bool copy_source(Ticket t,Recording binding,std::uintptr_t destination_identity,
                     Before&& before,Copy&& copy,After&& after){
        if(!color_ready(t,binding) || execute_called_ || !destination_identity ||
            destination_identity==binding.resource_identity)return false;
        if(!transition(t,binding,4,0x800))return false;
        copy_active_=true;
        try {
            before();copy();after(); // exact own helper batches/copy, no replay
        } catch(...) { copy_active_=false;failed_=true;throw; } // partial recording never gains readiness
        copy_active_=false;
        const bool restored=transition(t,binding,0x800,4);
        if(!restored)failed_=true;return restored;
    }
    bool before_execute(Ticket t,Recording binding,std::uintptr_t queue,bool closed){
        if(!color_ready(t,binding) || execute_called_ || queue!=t.input.queue || !closed)return false;
        execute_called_=true;return true;
    }
    bool execution_returned(Ticket t,Recording binding,std::uintptr_t queue,bool closed){
        if(!color_ready(t,binding) || !execute_called_ || executed_ || queue!=t.input.queue || !closed)return false;
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
        return exact(t) && available_ && !failed_ && !copy_active_ && !release_called_ &&
            (!execute_called_ || (executed_ && signalled_ && fence_value_));
    }
    template<class Call> XrResult release(Ticket t,Call&& call){
        if(!releasable(t))return XR_ERROR_CALL_ORDER_INVALID;
        release_called_=true;const auto result=call();
        released_=succeeded(result);loss_=loss_ || result==XR_SESSION_LOSS_PENDING;
        if(!released_ || loss_)failed_=true;return result;
    }
    bool layer_allowed(Ticket t)const{
        return exact(t) && released_ && !failed_ && !loss_ && !cleanup_only_ && recording_ && executed_ && signalled_;
    }
    void fail(Ticket t){if(exact(t))failed_=true;}
    bool pending()const{return active_.nonce && acquired_ && !released_;}
private:
    static std::uint32_t input_count(Ticket t){return t.input.images;}
};
} // namespace w3vr::xr_image
