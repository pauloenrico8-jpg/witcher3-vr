#pragma once
#include <type_traits>
#include <exception>
namespace w3vr::execute_observation {
enum class Phase { NotEntered, Entered, Returned };
struct NotForwarded {};
// One synchronous raw invocation, not a GPU/lifetime/unload receipt. A
// cooperating forwarder must pass this to its ACTUAL original call.
class Receipt {
    Phase phase_{Phase::NotEntered};
public:
    Receipt()=default;
    Receipt(const Receipt&)=delete;
    Receipt& operator=(const Receipt&)=delete;
    Phase phase()const noexcept{return phase_;}
    template<class Raw> bool forward_raw(Raw&& original){
        if(phase_!=Phase::NotEntered)return false;
        phase_=Phase::Entered;original();phase_=Phase::Returned;return true;
    }
    void require_returned()const{if(phase_!=Phase::Returned)throw NotForwarded{};}
};
struct Outcome {Phase phase;bool observation_failed;};
// Failed must retain/quarantine possibly submitted work without allocating.
// No retry after raw entry, even if that original throws. No SEH or recovery
// from a violated noexcept observer/cleanup contract is promised here.
template<class Body,class Raw,class Failed>
Outcome run(Body&& body,Raw&& raw,Failed&& failed)noexcept{
    static_assert(std::is_nothrow_invocable_v<Failed&,Phase>);
    Receipt receipt;bool failure{};
    try{body(receipt);}catch(...){failure=true;failed(receipt.phase());}
    if(receipt.phase()==Phase::NotEntered){
        if(!failure){failure=true;failed(receipt.phase());}
        try{receipt.forward_raw(raw);}catch(...){failure=true;failed(receipt.phase());}
    }
    return {receipt.phase(),failure};
}
// A destructor must not turn stack unwinding (or a missing raw call) into
// a post-submit publication. Publication itself may fail; quarantine instead.
template<class Publish,class Failed>
void after_returned(const Receipt& receipt,int entry_exceptions,Publish&& publish,Failed&& failed)noexcept{
    static_assert(std::is_nothrow_invocable_v<Failed&>);
    if(receipt.phase()!=Phase::Returned || std::uncaught_exceptions()!=entry_exceptions){failed();return;}
    try{publish();}catch(...){failed();}
}
} // namespace w3vr::execute_observation
