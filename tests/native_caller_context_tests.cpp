#include "native_caller_context.h"
namespace caller_context=w3vr::native_caller_context;
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <array>
extern "C" std::uint64_t lab_root(void*,std::uint64_t,std::uint64_t);
extern "C" char lab_root_after_call;
extern "C" char lab_root_fragment;
extern "C" char lab_worker;
extern "C" char lab_worker_after_call;
std::atomic<unsigned> checks{},callbacks{};
void require(bool ok,const char* why){checks.fetch_add(1);if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
struct Oracle {std::uint64_t identity{},value{};};
thread_local Oracle* oracle{};
__declspec(noinline) bool nested_capture(caller_context::Request request,caller_context::Snapshot& result){
    volatile std::uint64_t preserve=0x778899;
    const bool found=caller_context::capture(request,result);
    require(preserve==0x778899,"observer local changed");return found;
}
extern "C" __declspec(noinline) std::uint64_t lab_callback(void* receiver,void* descriptor,std::uint64_t mode){
    callbacks.fetch_add(1);require(oracle && receiver==oracle,"own original receiver changed");
    const caller_context::Request wanted{reinterpret_cast<std::uintptr_t>(&lab_root_after_call),
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),reinterpret_cast<std::uintptr_t>(&lab_root_fragment),24};
    caller_context::Snapshot snapshot{};require(nested_capture(wanted,snapshot),"actual unwind did not find own caller");
    require(snapshot.pc==wanted.return_pc && snapshot.image_base==wanted.image_base && snapshot.unwind_entry==wanted.unwind_entry,
        "different method/module or return accepted");
    require(snapshot.rdi==reinterpret_cast<std::uintptr_t>(oracle) && snapshot.rdi!=0x1122334455667788ULL,
        "worker RDI substituted for caller RDI");
    require(snapshot.rbp && snapshot.rbp-0x50==reinterpret_cast<std::uintptr_t>(descriptor),"caller RBP/local descriptor origin wrong");
    require(snapshot.sp>=snapshot.stack_low && snapshot.sp<snapshot.stack_high && snapshot.traversed>=2,"stack bounds/depth wrong");
    std::uint64_t values[2]{};std::memcpy(values,descriptor,sizeof(values));
    require(values[0]==oracle->value && values[1]==mode,"original argument bytes changed");
    require(snapshot.rbx==oracle->value,"late SAVE_NONVOL did not restore caller RBX");
    // The worker return starts with NOP: Windows must follow actual unwind
    // records here instead of taking its epilogue shortcut.
    DWORD64 worker_base{};auto* worker_entry=RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(&lab_worker_after_call),&worker_base,nullptr);
    require(worker_entry && worker_base==wanted.image_base,"actual worker lookup missing");
    unsigned fragments{},late_saves{};
    auto current=*worker_entry;
    for(unsigned chain=0;chain<8;++chain){
        auto* info=reinterpret_cast<const unsigned char*>(worker_base+current.UnwindData);
        require((info[0]&7)==1,"own unwind version wrong");
        const auto flags=info[0]>>3;
        if(!(flags&4)){require(worker_base+current.BeginAddress==reinterpret_cast<std::uintptr_t>(&lab_worker),"chain reached different primary");break;}
        ++fragments;require(!(flags&3),"chained fixture declares handler");
        if(info[2]){++late_saves;require(info[2]==2 && info[5]==0x34,"late save was not RBX SAVE_NONVOL");}
        std::memcpy(&current,info+4+((info[2]+1)&~1)*2,sizeof(current));
    }
    require(fragments==4 && late_saves==1,"own five-record chain was not exercised");
    require(*reinterpret_cast<const unsigned char*>(&lab_worker_after_call)==0x90,"worker return was epilogue shortcut");
    const auto sentinel=caller_context::Snapshot{1,2,3,4,5,6,7,8,9,10};
    for(unsigned kind=0;kind<6;++kind){
        auto rejected=wanted;
        if(kind==0)rejected.return_pc+=1;
        if(kind==1)rejected.image_base+=0x1000;
        if(kind==2)rejected.unwind_entry+=1;
        if(kind==3)rejected.max_frames=0;
        if(kind==4)rejected.max_frames=1;
        if(kind==5)rejected.unwind_entry=reinterpret_cast<std::uintptr_t>(&lab_root);
        auto unchanged=sentinel;require(!nested_capture(rejected,unchanged),"wrong caller/module/entry/budget admitted");
        require(unchanged.pc==sentinel.pc && unchanged.sp==sentinel.sp && unchanged.rdi==sentinel.rdi && unchanged.rbp==sentinel.rbp && unchanged.rbx==sentinel.rbx && unchanged.image_base==sentinel.image_base && unchanged.unwind_entry==sentinel.unwind_entry && unchanged.stack_low==sentinel.stack_low && unchanged.stack_high==sentinel.stack_high && unchanged.traversed==sentinel.traversed,"rejected capture partially published output");
    }
    return oracle->identity ^ values[0] ^ mode;
}
void run(std::uint64_t identity){
    Oracle value{identity,0xABCDEF0123456789ULL ^ identity};oracle=&value;
    for(std::uint64_t mode=0;mode<3;++mode)
        require(lab_root(&value,value.value,mode)==(value.identity^value.value^mode),"own root/worker return changed");
    oracle=nullptr;
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    require(sizeof(void*)==8,"x64 required");
    run(0x11);
    std::array<std::thread,2> readers{std::thread([]{run(0x22);}),std::thread([]{run(0x33);})};
    for(auto& thread:readers)thread.join();
    require(callbacks.load()==9,"unexpected own callback count");
    std::printf("PASS real Windows chained caller unwinding: %u checks, %u callbacks, 3 threads, exact caller/module/entry, restored RDI/RBP/RBX, 5-record worker chain and late save, local argument address, unchanged returns and rejected-output preservation\n",checks.load(),callbacks.load());
    std::puts("Frame code, opaque receiver and descriptor are OWN fixtures. Windows context/unwinding/thread stacks are REAL.");
    std::puts("SDK_INIT=false GAME=false HOOKS=false GPU=false OPENXR=false QUEST=false NATIVE_CLOCK_RECEIPT=false VR_FUNCTIONAL=false");
}
