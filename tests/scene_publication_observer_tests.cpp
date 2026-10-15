#include "temporal_record_leaf_bridge.h"
#include "scene_publication_observer.h"
#include "scene_publication_pair.h"
#include "native_caller_context.h"
#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <immintrin.h>
namespace leaf=w3vr::temporal_leaf;
extern "C" void* w3vr_scene_publication_original{};
extern "C" void publication_observer_fixture_fp_result();
extern "C" void publication_observer_fixture_clobber();
extern "C" void publication_observer_fixture_invoke(leaf::Frame*,void*);
extern "C" char publication_observer_fixture_after_call;
struct alignas(16) FpState { std::array<std::byte,512> bytes{}; };
extern "C" void publication_observer_fixture_invoke_fp(leaf::Frame*,void*,const FpState*,FpState*);
extern "C" void publication_observer_fixture_seed_fp();
extern "C" void publication_observer_fixture_clobber_fp();
std::atomic<unsigned> checks{},callbacks{},unwinds{},before_callbacks{},throw_calls{};
thread_local bool exceptional_run{};
extern "C" void publication_observer_fixture_throw(void*) noexcept(false);
extern "C" __declspec(noinline) void publication_observer_fixture_throw_body(void* p) noexcept(false){
    ++throw_calls;++*static_cast<unsigned*>(p);throw 47;
}
void require(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
thread_local std::uintptr_t destination{},source{};
thread_local std::array<std::uint64_t,7> expected_entry{};
extern "C" __declspec(noinline) void w3vr_scene_publication_before(w3vr::publication_observer::Frame* f) noexcept {
    ++before_callbacks;
    if(exceptional_run){require(f && f->entry_integer[1],"throw entry identity");return;}
    require(f && (reinterpret_cast<std::uintptr_t>(f)&15)==0,"entry observer alignment");
    require(f->entry_integer==expected_entry,"entry register state was not captured");
    require(f->owner==0 && f->serial==0 && f->pair==0 && f->generation==0,"stale observation admission");
    require(f->caller==reinterpret_cast<std::uintptr_t>(&publication_observer_fixture_after_call),"entry caller");
    f->owner=destination;f->serial=source;f->pair=19;f->generation=23;
    publication_observer_fixture_clobber_fp();
    _mm_setcsr((_mm_getcsr() & ~0x6000u) | 0x4000u | 0x20u);
    publication_observer_fixture_clobber();
}
extern "C" __declspec(noinline) void w3vr_scene_publication_after(w3vr::publication_observer::Frame* f) noexcept {
    ++callbacks;
    require(f && (reinterpret_cast<std::uintptr_t>(f)&15)==0,"return observer alignment");
    require(f->entry_integer==expected_entry && f->result_integer[0]==destination,"wrong entry/result identity");
    require(f->owner==destination && f->serial==source && f->pair==19 && f->generation==23,"observer local admission lost");
    require(f->caller==reinterpret_cast<std::uintptr_t>(&publication_observer_fixture_after_call),"return caller");
    const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    w3vr::native_caller_context::Snapshot root{};
    require(w3vr::native_caller_context::capture({f->caller,module,reinterpret_cast<std::uintptr_t>(&publication_observer_fixture_invoke),24},root),"observer unwind");
    ++unwinds;
    publication_observer_fixture_clobber_fp();
    _mm_setcsr((_mm_getcsr() & ~0x6000u) | 0x4000u | 0x20u);
    publication_observer_fixture_clobber();
}
void run(unsigned key,unsigned count){
    for(unsigned n=0;n<count;++n){
        alignas(16) std::array<std::byte,32> input{},output{};
        for(unsigned j=0;j<32;++j)input[j]=std::byte((key+n+j*13)&255);
        destination=reinterpret_cast<std::uintptr_t>(output.data());source=reinterpret_cast<std::uintptr_t>(input.data());
        leaf::Frame baseline{};baseline.integer={0xBADADEAD01020304ULL+n,destination,source,0x8E8E000000000000ULL+n,0x9999000000000000ULL+key,0xABCD010203040506ULL,0x1122334455667788ULL};
        for(unsigned k=0;k<16;++k)for(unsigned j=0;j<32;++j)baseline.vector[k][j]=std::byte((key+n+k*19+j*7)&255);
        auto observed=baseline;
        expected_entry=baseline.integer;
        // Seed real x87 registers, then vary legal control/status fields in an OWN image.
        // Restore the C++ caller's environment before continuing the test.
        FpState caller{},seed{},before{},after{};
        _fxsave64(caller.bytes.data());
        publication_observer_fixture_seed_fp();
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
        publication_observer_fixture_invoke_fp(&baseline,w3vr_scene_publication_original,&seed,&before);
        output.fill(std::byte{});
        publication_observer_fixture_invoke_fp(&observed,reinterpret_cast<void*>(&publication_observer_fixture_fp_result),&seed,&after);
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
        require(calls==1,"original own function executed zero or twice");
        std::uint64_t entry_rax{};std::memcpy(&entry_rax,output.data()+16,8);
        require(entry_rax==expected_entry[0],"entry observer changed original RAX");
    }
}
void test_pair_receipts(){
 namespace pp=w3vr::publication_pair;
 for(const auto* profile:{&w3vr::frame_preparation::remastered_500c,&w3vr::frame_preparation::remastered_1048522}){
  for(unsigned scenario=0;scenario<12;++scenario){
   pp::Identity id{profile,101,202,303,404,505};pp::Pair pair;pp::Ticket unchanged{{},999};
   require(!pair.before(id,true,unchanged) && unchanged.serial==999,"unarmed entry changed output");
   require(pair.stage(id) && !pair.stage(id),"staged identity replaced");
   require(pair.needs_cancel() && !pair.complete(),"constructor marked complete");
   require(!pair.before(id,true,unchanged),"primary entered without extra");
   if(scenario==0){
    require(!pair.extra_published(false) && pair.needs_cancel(),"failed extra lost cancellation");
    require(!pair.extra_published(true),"failed extra rearmed");pair.cancel();continue;
   }
   require(pair.extra_published(true) && !pair.complete(),"extra alone completed pair");
   auto foreign=id;foreign.command++;
   require(!pair.before(foreign,true,unchanged),"foreign command entered");
   auto copied=*profile;foreign=id;foreign.profile=&copied;
   require(!pair.before(foreign,true,unchanged),"copied profile entered");
   require(!pair.before(id,false,unchanged),"closed admission entered");
   pp::Ticket ticket{};require(pair.before(id,true,ticket) && ticket.serial,"primary receipt missing");
   require(!pair.before(id,true,unchanged),"nested/repeated publication replaced entry");
   auto stale=ticket;stale.serial++;
   require(!pair.after(stale,true,w3vr::scene_publication::unpublished),"old serial completed");
   stale=ticket;stale.identity.generation++;
   require(!pair.after(stale,true,w3vr::scene_publication::unpublished),"foreign generation completed");
   stale=ticket;stale.identity.pair++;
   require(!pair.after(stale,true,w3vr::scene_publication::unpublished),"foreign pair completed");
   if(scenario==1){pair.cancel();require(!pair.after(ticket,true,w3vr::scene_publication::unpublished) && !pair.needs_cancel(),"cancel revived");continue;}
   const bool ready=scenario!=2;
   const auto marker=scenario==3?0ULL:scenario==4?0x100000000ULL|w3vr::scene_publication::unpublished:w3vr::scene_publication::unpublished;
   const bool expected=ready && scenario!=3 && scenario!=4;
   require(pair.after(ticket,ready,marker)==expected,"primary result admission");
   require(pair.complete()==expected && pair.needs_cancel()!=expected,"completion/cancellation confusion");
   require(!pair.after(ticket,true,w3vr::scene_publication::unpublished),"repeated exit gained completion");
   require(!pair.before(id,true,unchanged) && !pair.extra_published(true),"finished pair rearmed");
   pair.cancel();require(!pair.needs_cancel(),"cleanup not acknowledged");
  }
  auto copied=*profile;
  for(auto bad:{pp::Identity{},pp::Identity{&copied,1,2,3,4,5},pp::Identity{profile,1,2,2,4,5},
    pp::Identity{profile,0,2,3,4,5},pp::Identity{profile,1,2,3,0,5}}){
   pp::Pair pair;require(!pair.stage(bad) && !pair.needs_cancel(),"invalid identity staged");
  }
 }
}
int main(){
    if(!leaf::available())return 77;
    test_pair_receipts();
    require(MH_Initialize()==MH_OK,"own MinHook initialize");
    require(MH_CreateHook(reinterpret_cast<void*>(&publication_observer_fixture_fp_result),reinterpret_cast<void*>(&w3vr_scene_publication_entry),&w3vr_scene_publication_original)==MH_OK && w3vr_scene_publication_original,"own create");
    require(MH_EnableHook(reinterpret_cast<void*>(&publication_observer_fixture_fp_result))==MH_OK,"own enable");
    run(11,32);
    std::array<std::thread,2> workers{std::thread([]{run(22,32);}),std::thread([]{run(33,32);})};
    for(auto& t:workers)t.join();
    require(before_callbacks==96 && callbacks==96 && unwinds==96,"own observer executed wrong number of times");
    // OWN threads only, joined before detaching. Not a game unload barrier.
    require(MH_DisableHook(reinterpret_cast<void*>(&publication_observer_fixture_fp_result))==MH_OK,"own disable");
    require(MH_RemoveHook(reinterpret_cast<void*>(&publication_observer_fixture_fp_result))==MH_OK,"own remove");
    require(MH_CreateHook(reinterpret_cast<void*>(&publication_observer_fixture_throw),reinterpret_cast<void*>(&w3vr_scene_publication_entry),&w3vr_scene_publication_original)==MH_OK,"throw own create");
    require(MH_EnableHook(reinterpret_cast<void*>(&publication_observer_fixture_throw))==MH_OK,"throw own enable");
    exceptional_run=true;
    for(unsigned n=0;n<32;++n){
        unsigned raw_count{};bool destroyed{},caught{};
        try{
            struct Guard{bool& value;~Guard(){value=true;}} guard{destroyed};
            publication_observer_fixture_throw(&raw_count);
        }catch(int value){caught=value==47;}
        require(caught && destroyed,"original C++ unwind lost guard or exception");
        require(raw_count==1,"throwing original repeated or skipped");
        require(callbacks==96,"post observer ran without original return");
    }
    exceptional_run=false;
    require(throw_calls==32 && before_callbacks==128,"throwing original observer counts");
    require(MH_DisableHook(reinterpret_cast<void*>(&publication_observer_fixture_throw))==MH_OK,"throw own disable");
    require(MH_RemoveHook(reinterpret_cast<void*>(&publication_observer_fixture_throw))==MH_OK,"throw own remove");
    require(MH_Uninitialize()==MH_OK,"own uninitialize");
    std::printf("PASS scene publication observer: %u checks, 96 before/after real own MinHook observations/unwinds, 192 own original calls, 3 threads; 32 additional actual original C++ unwinds (no post observation); seven volatile GPRs, sixteen AVX registers, CPU flags, MXCSR and x87 control/status/tags/data preserved across four rounding modes.\n",checks.load());
    std::puts("GAME=false SDK_INIT=false GPU=false OPENXR=false QUEST=false VR_FUNCTIONAL=false");
}
