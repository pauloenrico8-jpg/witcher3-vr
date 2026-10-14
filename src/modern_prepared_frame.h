#pragma once
#include <cstdint>
#include "xr_frame_identity.h"
namespace w3vr::prepared_frame {
struct Identity {std::int64_t time{};std::uint64_t pair{};std::uint32_t generation{};xr_frame::Identity xr{};bool operator==(const Identity&)const=default;};
struct Ticket {Identity identity{};std::uint64_t nonce{};};
inline bool valid(Identity i){return xr_frame::valid(i.xr) && i.time>0 && i.time!=INT64_MAX && i.pair && i.pair!=UINT64_MAX && i.generation;}
// CPU ownership of an actual already-begun XR frame, not engine/COM/GPU life.
// Caller serializes the state. Never synthesize time or cancel through timeout.
class Binding {
    Ticket active_{};xr_frame::Identity last_reserved_xr_{};std::uint64_t nonce_{};bool cancelled_{},dispatched_{};
    bool exact(Ticket t)const{return t.nonce && t.nonce==active_.nonce && t.identity==active_.identity;}
public:
    bool reserve(Identity i,bool prepared,std::int64_t actual_time,xr_frame::Identity actual_frame,Ticket& out){
        if(!prepared || actual_frame!=i.xr || actual_time!=i.time || !valid(i) || active_.nonce || nonce_==UINT64_MAX)return false;
        if(xr_frame::valid(last_reserved_xr_) && !xr_frame::newer(i.xr,last_reserved_xr_))return false;
        last_reserved_xr_=i.xr;active_={i,++nonce_};cancelled_=dispatched_=false;out=active_;return true;
    }
    bool cancel(Ticket t){if(!exact(t) || dispatched_)return false;cancelled_=true;return true;}
    bool dispatched(Ticket t){if(!exact(t) || cancelled_)return false;dispatched_=true;return true;}
    bool allows(Identity i)const{return active_.nonce && valid(i) && active_.identity==i && dispatched_ && !cancelled_;}
    bool cancelled_for(std::int64_t actual_time,std::uint32_t generation,xr_frame::Identity frame)const{
        return active_.nonce && active_.identity.time==actual_time && active_.identity.xr==frame && (cancelled_ || active_.identity.generation!=generation);
    }
    // A successful runtime session stop invalidates this CPU frame binding.
    // It does not retire GPU sources; keep the nonce to reject old callbacks.
    bool runtime_stopped(bool xr_success) {
        if (!xr_success) return false;
        active_ = {}; cancelled_ = dispatched_ = false;
        return true;
    }
    bool end(std::int64_t actual_time,xr_frame::Identity frame,bool xr_success){
        if(!active_.nonce || active_.identity.time!=actual_time || active_.identity.xr!=frame || !xr_success)return false;
        active_={};cancelled_=dispatched_=false;return true;
    }
};
} // namespace w3vr::prepared_frame
