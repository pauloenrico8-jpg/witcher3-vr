#include "engine_producer_bridge.h"
#include "native_caller_context.h"
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <array>
namespace bridge=w3vr::producer_bridge;
extern "C" void producer_fixture_target();
extern "C" void producer_fixture_clobber();
extern "C" char w3vr_invoke_producer_after_call;
struct alignas(16) Context {bridge::Frame observed{},prescribed{};std::uint64_t calls{};};
static_assert(offsetof(Context,prescribed)==0x130 && offsetof(Context,calls)==0x260);
void* original{};
std::atomic<unsigned> checks{},body_calls{},cleanup_calls{},unwinds{};
thread_local Context* active{};
void require(bool ok,const char* reason){++checks;if(!ok){std::printf("FAIL %s\n",reason);std::exit(1);}}
extern "C" __declspec(noinline) void w3vr_producer_scope_body(bridge::Frame* frame){
    ++body_calls;require(frame && active && frame->entry_integer[0]==reinterpret_cast<std::uintptr_t>(active),"foreign entry receiver");
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    w3vr::native_caller_context::Snapshot snapshot{};
    require(w3vr::native_caller_context::capture({reinterpret_cast<std::uintptr_t>(&w3vr_invoke_producer_after_call),base,
        reinterpret_cast<std::uintptr_t>(&w3vr_invoke_producer_original),24},snapshot),"registered bridge frame could not be unwound");
    ++unwinds;
    producer_fixture_clobber();
    w3vr_invoke_producer_original(original,frame);
    const auto returned=*frame;
    producer_fixture_clobber(); // Deliberately overwrite ALL standard return registers after the original.
    ++cleanup_calls;
    require(frame->return_integer==returned.return_integer && frame->return_vector==returned.return_vector,"cleanup altered saved result");
}
void run(std::uint64_t key,unsigned count){
    Context context{};active=&context;
    for(unsigned n=0;n<count;++n){
        bridge::Frame input{};input.entry_integer={reinterpret_cast<std::uintptr_t>(&context),key^n,0xF0F1123456789ABCULL^n,0xDEAD000000000000ULL|n};
        context.prescribed.return_integer={0x8000000000000000ULL^(key*17+n),0xFEDCBA9876543210ULL^(key+n)};
        for(unsigned lane=0;lane<4;++lane)for(unsigned byte=0;byte<32;++byte){
            input.entry_vector[lane][byte]=std::byte((key+n+lane*59+byte*7)&255);
            context.prescribed.return_vector[lane][byte]=std::byte((key^n^(lane*41+byte*19))&255);
        }
        // IEEE payloads are bits, not converted values: quiet/signaling NaNs,
        // signed zero, infinities and upper AVX lanes must survive unchanged.
        const std::array<std::uint32_t,8> patterns{0x7FC12345,0x7FAABCDE,0x80000000,0x7F800000,0xFF800000,0,0xBF800000,0x3F800001};
        std::memcpy(input.entry_vector[1].data(),patterns.data(),32);
        std::memcpy(context.prescribed.return_vector[0].data(),patterns.data(),32);
        const auto before=context.calls;
        w3vr_invoke_producer_original(reinterpret_cast<void*>(&producer_fixture_target),&input);
        require(context.calls==before+1,"original executed zero/twice");
        require(context.observed.entry_integer==input.entry_integer && context.observed.entry_vector==input.entry_vector,"actual Native fixture received changed arguments");
        require(input.return_integer==context.prescribed.return_integer,"integer/pointer/boolean return bits lost");
        require(input.return_vector==context.prescribed.return_vector,"float/double/vector/upper AVX return bits lost");
    }
    active=nullptr;
}
int main(){
    if(!bridge::available()){std::puts("SKIP: OS AVX register transport unavailable");return 77;}
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    run(0x11,2);require(!body_calls && !cleanup_calls,"unhooked fixture entered sidecar");
    require(MH_Initialize()==MH_OK,"own MinHook initialize");
    require(MH_CreateHook(reinterpret_cast<void*>(&producer_fixture_target),reinterpret_cast<void*>(&w3vr_producer_entry),&original)==MH_OK && original,"own MinHook create");
    require(MH_EnableHook(reinterpret_cast<void*>(&producer_fixture_target))==MH_OK,"own MinHook enable");
    run(0x22,32);
    std::array<std::thread,2> workers{std::thread([]{run(0x33,32);}),std::thread([]{run(0x44,32);})};
    for(auto& worker:workers)worker.join();
    require(body_calls==96 && cleanup_calls==96 && unwinds==96,"sidecar/unwind cardinality changed");
    // Only this executable's threads have drained/joined. Not a Game unload barrier.
    require(MH_DisableHook(reinterpret_cast<void*>(&producer_fixture_target))==MH_OK,"own disable after join");
    require(MH_RemoveHook(reinterpret_cast<void*>(&producer_fixture_target))==MH_OK,"own remove after join");
    require(MH_Uninitialize()==MH_OK,"own MinHook uninitialize");
    run(0x55,2);require(body_calls==96,"removed own hook still active");
    std::printf("PASS producer raw return bridge: %u checks, 100 original calls, 96 real hook callbacks/unwinds, 3 threads, unchanged four integer and four AVX input registers, unchanged raw integer/vector returns after deliberate cleanup clobber\n",checks.load());
    std::puts("Windows/MinHook/ASM OWN runtime real. Game signatures, object lifecycle and Game execution NOT validated.");
    std::puts("GAME=false SDK_INIT=false GPU=false OPENXR=false QUEST=false VR_FUNCTIONAL=false");
}
