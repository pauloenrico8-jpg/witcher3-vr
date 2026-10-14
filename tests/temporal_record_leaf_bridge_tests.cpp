#include "temporal_record_leaf_bridge.h"
#include "native_caller_context.h"
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <immintrin.h>
namespace leaf=w3vr::temporal_leaf;
extern "C" void* w3vr_temporal_leaf_original{};
extern "C" void temporal_leaf_fixture_fp_result();
extern "C" void temporal_leaf_fixture_clobber();
extern "C" void temporal_leaf_fixture_invoke(leaf::Frame*,void*);
extern "C" char temporal_leaf_fixture_after_call;
struct alignas(16) FpState { std::array<std::byte,512> bytes{}; };
extern "C" void temporal_leaf_fixture_invoke_fp(leaf::Frame*,void*,const FpState*,FpState*);
extern "C" void temporal_leaf_fixture_seed_fp();
extern "C" void temporal_leaf_fixture_clobber_fp();
std::atomic<unsigned> checks{},callbacks{},unwinds{};
void require(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
thread_local std::uintptr_t destination{},source{};
extern "C" __declspec(noinline) void w3vr_temporal_leaf_after(leaf::Frame* f){
    ++callbacks;
    require(f && (reinterpret_cast<std::uintptr_t>(f)&15)==0,"observer frame/stack lost 16-byte alignment");
    require(f && f->destination==destination && f->source==source && f->integer[0]==destination,"wrong exact post-call inputs/result");
    require(f->caller==reinterpret_cast<std::uintptr_t>(&temporal_leaf_fixture_after_call),"caller return changed");
    auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    w3vr::native_caller_context::Snapshot root{};
    require(w3vr::native_caller_context::capture({f->caller,module,reinterpret_cast<std::uintptr_t>(&temporal_leaf_fixture_invoke),24},root),"post-call frame did not unwind through registered flags save");
    ++unwinds;
    const auto saved=*f;
    temporal_leaf_fixture_clobber_fp();
    _mm_setcsr((_mm_getcsr() & ~0x6000u) | 0x4000u | 0x20u);
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
        // Seed real x87 registers, then vary legal control/status fields in an OWN image.
        // Restore the C++ caller's environment before continuing the test.
        FpState caller{},seed{},before{},after{};
        _fxsave64(caller.bytes.data());
        temporal_leaf_fixture_seed_fp();
        _fxsave64(seed.bytes.data());
        _fxrstor64(caller.bytes.data());
        const auto rounding=n%4;
        const std::uint16_t precision=(n%3==0 ? 0u : n%3==1 ? 0x200u : 0x300u);
        const std::uint16_t control=static_cast<std::uint16_t>(0x7Fu | precision | (rounding<<10));
        std::memcpy(seed.bytes.data(),&control,2);
        std::uint16_t status{};std::memcpy(&status,seed.bytes.data()+2,2);
        status=static_cast<std::uint16_t>(status | (n&0x3Fu)); // masked sticky exceptions
        std::memcpy(seed.bytes.data()+2,&status,2);
        unsigned mask{};std::memcpy(&mask,seed.bytes.data()+28,4);
        unsigned mxcsr=0x1F80u | (rounding<<13) | (n&0x3Fu);
        if(n&4)mxcsr|=(mask&0x8040u); // only supported DAZ/FTZ
        std::memcpy(seed.bytes.data()+24,&mxcsr,4);
        // Trampoline baseline and hook both start with the SAME environment.
        temporal_leaf_fixture_invoke_fp(&baseline,w3vr_temporal_leaf_original,&seed,&before);
        output.fill(std::byte{});
        temporal_leaf_fixture_invoke_fp(&observed,reinterpret_cast<void*>(&temporal_leaf_fixture_fp_result),&seed,&after);
        require(std::memcmp(seed.bytes.data()+32,before.bytes.data()+32,10)!=0,"own original did not produce distinct x87 data");
        require(std::memcmp(before.bytes.data(),after.bytes.data(),5)==0,"x87 control/status/tag changed");
        require(std::memcmp(before.bytes.data()+6,after.bytes.data()+6,26)==0,"x87 opcode/pointers or MXCSR/mask changed");
        for(unsigned st=0;st<8;++st)
            require(std::memcmp(before.bytes.data()+32+st*16,after.bytes.data()+32+st*16,10)==0,"x87 register payload changed");
        FpState restored{};_fxsave64(restored.bytes.data());
        require(std::memcmp(caller.bytes.data(),restored.bytes.data(),5)==0 &&
            std::memcmp(caller.bytes.data()+24,restored.bytes.data()+24,4)==0,"own FP harness leaked its test environment");
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
    require(MH_CreateHook(reinterpret_cast<void*>(&temporal_leaf_fixture_fp_result),reinterpret_cast<void*>(&w3vr_temporal_leaf_entry),&w3vr_temporal_leaf_original)==MH_OK && w3vr_temporal_leaf_original,"own create");
    require(MH_EnableHook(reinterpret_cast<void*>(&temporal_leaf_fixture_fp_result))==MH_OK,"own enable");
    run(11,32);
    std::array<std::thread,2> workers{std::thread([]{run(22,32);}),std::thread([]{run(33,32);})};
    for(auto& t:workers)t.join();
    require(callbacks==96 && unwinds==96,"own observer executed wrong number of times");
    // OWN threads only, joined before detaching. Not a game unload barrier.
    require(MH_DisableHook(reinterpret_cast<void*>(&temporal_leaf_fixture_fp_result))==MH_OK,"own disable");
    require(MH_RemoveHook(reinterpret_cast<void*>(&temporal_leaf_fixture_fp_result))==MH_OK,"own remove");
    require(MH_Uninitialize()==MH_OK,"own uninitialize");
    std::printf("PASS temporal record leaf bridge: %u checks, 96 real own MinHook callbacks/unwinds, 192 own leaf calls, 3 threads; seven volatile GPRs, sixteen AVX registers, CPU flags, MXCSR and x87 control/status/tags/data preserved across four rounding modes.\n",checks.load());
    std::puts("GAME=false SDK_INIT=false GPU=false OPENXR=false QUEST=false VR_FUNCTIONAL=false");
}
