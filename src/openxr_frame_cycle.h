#pragma once
#include <openxr/openxr.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <utility>
#include <thread>
namespace w3vr::xr_frame_cycle {
struct Ticket {
    XrSession session{};std::uint64_t epoch{},nonce{};std::uint32_t generation{};
    bool operator==(const Ticket&)const=default;
    explicit operator bool()const{return session && epoch && nonce && generation;}
};
struct Payload {
    Ticket ticket{};XrFrameState state{XR_TYPE_FRAME_STATE};
    std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};
    XrViewStateFlags flags{};bool views_valid{},discarded{};
    bool renderable()const{
        constexpr auto required=XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_VALID_BIT;
        return views_valid && (flags&required)==required && !discarded && state.shouldRender==XR_TRUE;
    }
};
enum class Phase {Stopped,Idle,Preparing,Ready,Presenting,Failed};
// Own CPU protocol only. No callbacks under state_; wait and begin/end have
// separate serialization lanes. Does not retain Game/GPU/loader lifetimes.
class Cycle {
    mutable std::mutex state_;std::mutex wait_lane_,begin_end_lane_;
    XrSession session_{};std::uint64_t epoch_{},nonce_{};XrTime last_time_{};
    Phase phase_{Phase::Stopped};bool accepting_{},stop_requested_{},stop_attempted_{};
    unsigned in_flight_{};Ticket active_{};Payload ready_{};
    bool waited_{},begun_{},discarded_{},wait_called_{},begin_called_{},end_called_{},stop_called_{};
    bool exact(Ticket t)const{return t && t==active_ && t.session==session_ && t.epoch==epoch_;}
public:
    class Lease {
        friend class Cycle;Cycle* owner_{};Ticket ticket_{};bool presenting_{},done_{};std::thread::id thread_{};
        Lease(Cycle* o,Ticket t,bool present):owner_(o),ticket_(t),presenting_(present),thread_(std::this_thread::get_id()){}
    public:
        Lease()=default;Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
        Lease(Lease&& v)noexcept:owner_(std::exchange(v.owner_,nullptr)),ticket_(v.ticket_),presenting_(v.presenting_),done_(v.done_),thread_(v.thread_){}
        Lease& operator=(Lease&&)=delete;
        ~Lease(){if(owner_)owner_->release(*this);}
        explicit operator bool()const{return owner_!=nullptr;}
        Ticket ticket()const{return ticket_;}
    };
    bool session_started(XrSession session) {
        std::lock_guard lock{state_};
        if(!session || phase_!=Phase::Stopped || in_flight_ || epoch_==UINT64_MAX)return false;
        session_=session;++epoch_;last_time_=0;active_={};ready_={};
        phase_=Phase::Idle;accepting_=true;stop_requested_=stop_attempted_=stop_called_=false;return true;
    }
    Lease prepare(XrSession session,std::uint32_t generation) {
        std::lock_guard lock{state_};
        if(!accepting_ || session!=session_ || !generation || phase_!=Phase::Idle || in_flight_ || nonce_==UINT64_MAX)return {};
        active_={session,epoch_,++nonce_,generation};phase_=Phase::Preparing;
        waited_=begun_=discarded_=wait_called_=begin_called_=end_called_=false;++in_flight_;return {this,active_,false};
    }
    template<class Call> XrResult wait(Lease& lease,Call&& call) {
        {std::lock_guard lock{state_};if(lease.owner_!=this || lease.thread_!=std::this_thread::get_id() || lease.done_ || lease.presenting_ || !exact(lease.ticket_) || phase_!=Phase::Preparing || !accepting_ || wait_called_)return XR_ERROR_CALL_ORDER_INVALID;wait_called_=true;}
        XrResult result;{std::lock_guard lane{wait_lane_};result=call();}
        std::lock_guard lock{state_};
        if(result==XR_SUCCESS)waited_=true;else phase_=Phase::Failed;
        return result;
    }
    template<class Call> XrResult begin(Lease& lease,Call&& call) {
        {std::lock_guard lock{state_};if(lease.owner_!=this || lease.thread_!=std::this_thread::get_id() || lease.done_ || lease.presenting_ || !exact(lease.ticket_) || phase_!=Phase::Preparing || !waited_ || begin_called_)return XR_ERROR_CALL_ORDER_INVALID;begin_called_=true;}
        XrResult result;{std::lock_guard lane{begin_end_lane_};result=call();}
        std::lock_guard lock{state_};
        begun_=result==XR_SUCCESS || result==XR_FRAME_DISCARDED;discarded_=result==XR_FRAME_DISCARDED;
        if(!begun_)phase_=Phase::Failed;return result;
    }
    // Values copied; no next chain or borrowed view pointer crosses the scope.
    bool publish(Lease& lease,Payload payload) {
        std::lock_guard lock{state_};
        if(lease.owner_!=this || lease.thread_!=std::this_thread::get_id() || lease.done_ || lease.presenting_ || !exact(lease.ticket_) || phase_!=Phase::Preparing || !begun_)return false;
        if(payload.state.type!=XR_TYPE_FRAME_STATE || payload.state.next || payload.state.predictedDisplayTime<=last_time_ ||
           payload.state.predictedDisplayTime==INT64_MAX || payload.state.predictedDisplayPeriod<=0 ||
           (payload.state.shouldRender!=XR_FALSE && payload.state.shouldRender!=XR_TRUE))return false;
        for(const auto& view:payload.views)if(view.type!=XR_TYPE_VIEW || view.next)return false;
        payload.ticket=lease.ticket_;payload.discarded=discarded_;ready_=payload;
        last_time_=payload.state.predictedDisplayTime;phase_=Phase::Ready;lease.done_=true;return accepting_;
    }
    bool snapshot(XrSession session,Payload& out)const {
        std::lock_guard lock{state_};
        if(!accepting_ || session!=session_ || phase_!=Phase::Ready || in_flight_)return false;
        out=ready_;return true;
    }
    Lease present(XrSession session,Payload& out) {
        std::lock_guard lock{state_};
        if(!accepting_ || session!=session_ || phase_!=Phase::Ready || in_flight_)return {};
        out=ready_;phase_=Phase::Presenting;++in_flight_;return {this,active_,true};
    }
    bool current(Ticket t)const {
        std::lock_guard lock{state_};return accepting_ && exact(t) && (phase_==Phase::Ready || phase_==Phase::Presenting);
    }
    template<class Call> XrResult end(Lease& lease,Call&& call) {
        {std::lock_guard lock{state_};if(lease.owner_!=this || lease.thread_!=std::this_thread::get_id() || lease.done_ || !lease.presenting_ || !exact(lease.ticket_) || phase_!=Phase::Presenting || !begun_ || end_called_)return XR_ERROR_CALL_ORDER_INVALID;end_called_=true;}
        XrResult result;{std::lock_guard lane{begin_end_lane_};result=call();}
        std::lock_guard lock{state_};lease.done_=true;
        phase_=result==XR_SUCCESS?Phase::Idle:Phase::Failed;ready_={};active_={};
        return result; // Failure is sticky until a successful session stop.
    }
    void request_stop(XrSession session) {
        std::lock_guard lock{state_};if(session!=session_ || phase_==Phase::Stopped)return;
        accepting_=false;stop_requested_=true;
    }
    // Nonblocking: poll again after current leases leave. No wait holds a lane
    // needed by the callback whose completion would drain the leases.
    std::uint64_t claim_stop(XrSession session) {
        std::lock_guard lock{state_};
        if(session!=session_ || !stop_requested_ || stop_attempted_ || in_flight_)return 0;
        stop_attempted_=true;return epoch_;
    }
    template<class Call> XrResult stop(XrSession session,std::uint64_t epoch,Call&& call) {
        {std::lock_guard lock{state_};if(!epoch || session!=session_ || epoch!=epoch_ || !stop_attempted_ || !stop_requested_ || in_flight_ || stop_called_)return XR_ERROR_CALL_ORDER_INVALID;stop_called_=true;}
        XrResult result;{std::lock_guard lane{begin_end_lane_};result=call();}
        std::lock_guard lock{state_};
        if(result==XR_SUCCESS || result==XR_SESSION_LOSS_PENDING){phase_=Phase::Stopped;active_={};ready_={};}
        return result;
    }
private:
    void release(Lease& lease) {
        std::lock_guard lock{state_};
        if(!lease.done_ && exact(lease.ticket_)){phase_=Phase::Failed;ready_={};}
        if(in_flight_)--in_flight_;
    }
};
} // namespace w3vr::xr_frame_cycle
