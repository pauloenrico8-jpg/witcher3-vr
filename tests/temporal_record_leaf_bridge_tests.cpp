#include "temporal_record_leaf_bridge.h"
#include "native_caller_context.h"
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
namespace leaf=w3vr::temporal_leaf;
extern "C" void* w3vr_temporal_leaf_original{};
extern "C" void temporal_leaf_fixture();
extern "C" void temporal_leaf_fixture_clobber();
extern "C" void temporal_leaf_fixture_invoke(leaf::Frame*,void*);
extern "C" char temporal_leaf_fixture_after_call;
std::atomic<unsigned> checks{},callbacks{},unwinds{};
void require(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
thread_local std::uintptr_t destination{},source{};
extern "C" __declspec(noinline) void w3vr_temporal_leaf_after(leaf::Frame* f){
    ++callbacks;
    require(f && f->destination==destination && f->source==source && f->integer[0]==destination,"wrong exact post-call inputs/result");
    require(f->caller==reinterpret_cast<std::uintptr_t>(&temporal_leaf_fixture_after_call),"caller return changed");
    auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    w3vr::native_caller_context::Snapshot root{};
    require(w3vr::native_caller_context::capture({f->caller,module,reinterpret_cast<std::uintptr_t>(&temporal_leaf_fixture_invoke),24},root),"post-call frame did not unwind through registered flags save");
    ++unwinds;
    const auto saved=*f;
    temporal_leaf_fixture_clobber();
    require(f->integer==saved.integer && f->vector==saved.vector,"observer corrupted its own saved output");
}
void run(unsigned key,unsigned count){
    for(unsigned n=0;n<count;++n){
        alignas(16) std::array<std::byte,32> input{},output{};
        for(unsigned j=0;j<32;++j)input[j]=std::byte((key+n+j*13)&255);
        destination=reinterpret_cast<std::uintptr_t>(output.data());source=reinterpret_cast<std::uintptr_t>(input.data());
        leaf::Frame baseline{};baseline.integer={0,destination,source,0x8E8E000000000000ULL+n,0x9999000000000000ULL+key,0xABCD010203040506ULL,0x1122334455667788ULL};
        for(unsigned k=0;k<16;++k)for(unsigned j=0;j<32;++j)baseline.vector[k][j]=std::byte((key+n+k*19+j*7)&255);
        auto observed=baseline;
        // Baseline OWN leaf called through the created trampoline, not through the hook.
        temporal_leaf_fixture_invoke(&baseline,w3vr_temporal_leaf_original);
        output.fill(std::byte{});
        temporal_leaf_fixture_invoke(&observed,reinterpret_cast<void*>(&temporal_leaf_fixture));
        require(observed.integer==baseline.integer,"volatile pointer/integer/R8 changed by post-call observer");
        require(observed.vector==baseline.vector,"AVX lane including upper nonvolatile lanes changed");
        require((observed.reserved&0x8D5)==(baseline.reserved&0x8D5),"arithmetic CPU flags changed");
        require(std::memcmp(output.data(),input.data(),16)==0,"original own leaf not executed before observation");
        std::uint64_t calls{};std::memcpy(&calls,output.data()+24,sizeof(calls));
        require(calls==1,"original own leaf executed zero or twice");
    }
}
int main(){
    if(!leaf::available())return 77;
    require(MH_Initialize()==MH_OK,"own MinHook initialize");
    require(MH_CreateHook(reinterpret_cast<void*>(&temporal_leaf_fixture),reinterpret_cast<void*>(&w3vr_temporal_leaf_entry),&w3vr_temporal_leaf_original)==MH_OK && w3vr_temporal_leaf_original,"own create");
    require(MH_EnableHook(reinterpret_cast<void*>(&temporal_leaf_fixture))==MH_OK,"own enable");
    run(11,32);
    std::array<std::thread,2> workers{std::thread([]{run(22,32);}),std::thread([]{run(33,32);})};
    for(auto& t:workers)t.join();
    require(callbacks==96 && unwinds==96,"own observer executed wrong number of times");
    // OWN threads only, joined before detaching. Not a game unload barrier.
    require(MH_DisableHook(reinterpret_cast<void*>(&temporal_leaf_fixture))==MH_OK,"own disable");
    require(MH_RemoveHook(reinterpret_cast<void*>(&temporal_leaf_fixture))==MH_OK,"own remove");
    require(MH_Uninitialize()==MH_OK,"own uninitialize");
    std::printf("PASS temporal record leaf bridge: %u checks, 96 real own MinHook callbacks/unwinds, 192 own leaf calls, 3 threads; seven volatile GPRs, sixteen AVX registers and CPU flags preserved.\n",checks.load());
    std::puts("GAME=false SDK_INIT=false GPU=false OPENXR=false QUEST=false VR_FUNCTIONAL=false");
}
