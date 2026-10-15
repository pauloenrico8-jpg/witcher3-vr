#pragma once
#include <type_traits>
#include <utility>
namespace w3vr::capture_recording {
// C++ exception boundary only. Caller supplies serialized lifetime bookkeeping;
// this does not catch SEH, retain resources, cancel GPU work or authorize unload.
struct InternalScope {
    bool& flag;bool previous;
    explicit InternalScope(bool& value)noexcept:flag(value),previous(value){flag=true;}
    ~InternalScope()noexcept{flag=previous;}
    InternalScope(const InternalScope&)=delete;
};
template<class Finish> struct CpuScope {
    Finish& finish;
    ~CpuScope()noexcept{finish();}
};
template<class Prepare,class Record,class Publish,class Failed,class Finish>
bool run(bool& internal,Prepare&& prepare,Record&& record,Publish&& publish,
    Failed&& failed,Finish&& finish)noexcept{
    static_assert(std::is_nothrow_invocable_v<Failed&,bool>);
    static_assert(std::is_nothrow_invocable_v<Finish&>);
    CpuScope<Finish> cpu{finish};bool commands_may_reference{};
    try{
        if(!prepare()){failed(false);return false;}
        commands_may_reference=true;
        {InternalScope scope{internal};record();}
        if(!publish()){failed(true);return false;}
        return true;
    }catch(...){failed(commands_may_reference);return false;}
}
template<class Observe,class Failed>
void observe(Observe&& operation,Failed&& failed)noexcept{
    static_assert(std::is_nothrow_invocable_v<Failed&>);
    try{operation();}catch(...){failed();}
}
} // namespace w3vr::capture_recording
