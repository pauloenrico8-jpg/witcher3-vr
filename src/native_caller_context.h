#pragma once
#include <windows.h>
#include <cstdint>
namespace w3vr::native_caller_context {
struct Request {std::uintptr_t return_pc{},image_base{},unwind_entry{};unsigned max_frames{24};};
struct Snapshot {std::uintptr_t pc{},sp{},rdi{},rbp{},rbx{},image_base{},unwind_entry{},stack_low{},stack_high{};unsigned traversed{};};
// Current thread only. No game calls, no register editing/restoration, no
// guessed private stack offsets and no language-handler execution. Result is
// a borrowed CPU observation, not a clock/descriptor/object/GPU/unload lease.
__declspec(noinline) inline bool capture(Request request,Snapshot& result) noexcept {
    if(!request.return_pc || !request.image_base || !request.unwind_entry ||
        request.max_frames==0 || request.max_frames>48)return false;
    __try {
        ULONG_PTR low{},high{};GetCurrentThreadStackLimits(&low,&high);
        CONTEXT context{};RtlCaptureContext(&context);
        for(unsigned step=0;step<request.max_frames;++step){
            if(!context.Rip || context.Rsp<low || context.Rsp>=high || (context.Rsp&7))return false;
            DWORD64 base{};auto* entry=RtlLookupFunctionEntry(context.Rip,&base,nullptr);
            // An unregistered frame/trampoline is NOT an ordinary function
            // whose private frame layout may safely be guessed.
            if(!entry || !base)return false;
            if(context.Rip==request.return_pc){
                if(base!=request.image_base || base+entry->BeginAddress!=request.unwind_entry)return false;
                const Snapshot candidate{context.Rip,context.Rsp,context.Rdi,context.Rbp,context.Rbx,base,
                    base+entry->BeginAddress,low,high,step};
                result=candidate;return true;
            }
            const auto previous_sp=context.Rsp;
            void* handler_data{};DWORD64 establisher{};
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,entry,&context,&handler_data,&establisher,nullptr);
            if(context.Rsp<=previous_sp || context.Rsp>=high)return false;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return false;
}
} // namespace w3vr::native_caller_context
