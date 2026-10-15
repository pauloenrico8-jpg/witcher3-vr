#include "engine_scene_publication.h"
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
namespace pub=w3vr::scene_publication;
std::atomic<unsigned> checks{},calls{},caught{};
void require(bool value,const char* why){++checks;if(!value){std::cerr<<why<<'\n';std::abort();}}
struct Context {std::uint32_t mode{},calls{};std::uint64_t header{},entry_rax{},sentinel{},receiver{};};
static_assert(offsetof(Context,entry_rax)==0x10 && offsetof(Context,sentinel)==0x18 && offsetof(Context,receiver)==0x20);
extern "C" void w3vr_publication_fixture(void*) noexcept(false);
extern "C" void w3vr_publication_throw_fixture(void*) noexcept(false){++caught;throw std::runtime_error("OWN callback failure");}
void invocation_cases(){
    for(unsigned n=0;n<64;++n)for(unsigned mode=0;mode<5;++mode){
        Context c{mode,0,mode==3?0x45584543:pub::unpublished,UINT64_MAX,0x140000000ull+0x05C92B30,0};
        bool threw{};std::uint64_t result=UINT64_MAX;
        try{result=w3vr_invoke_scene_publication(w3vr_publication_fixture,&c);}
        catch(const std::runtime_error&){threw=true;}
        require(c.calls==1 && c.entry_rax==0 && c.receiver==reinterpret_cast<std::uintptr_t>(&c),
            "Raw entry repeated, volatile seed lost, or receiver changed");
        require(threw==(mode==4),"OWN original exception/unwind was changed");
        if(threw)continue;
        require(pub::published(result)==(mode==0),"Void return or wrong previous marker became publication");
        if(mode==0)require(c.header==0 && result==pub::unpublished,"Post-call recycled memory was used as a receipt");
        if(mode==1)require(result==0 && c.header==pub::unpublished,"No-write path was fabricated as published");
        if(mode==2)require(result==c.sentinel,"Sentinel register bits changed");
        if(mode==3)require(result==0x45584543 && c.header==0,"Wrong previous marker authorized publication");
        ++calls;
    }
}
std::array<std::uint8_t,pub::code_bytes> code(const w3vr::frame_submission::Profile* p){
    auto bytes=pub::shape;const auto a=static_cast<std::int32_t>(p->renderer_global-p->dispatch-16);
    const auto b=static_cast<std::int32_t>(p->unavailable-p->dispatch-28);
    std::memcpy(bytes.data()+12,&a,4);std::memcpy(bytes.data()+24,&b,4);return bytes;
}
void shape_cases(){
    for(const auto* p:{&w3vr::frame_submission::remastered_500c,&w3vr::frame_submission::remastered_1048522}){
        auto bytes=code(p);const std::uintptr_t module=0x140000000;
        require(pub::matches(p,module,bytes),"Known instruction-shape fixture rejected");
        auto copied=*p;require(!pub::matches(&copied,module,bytes)&&!pub::matches(nullptr,module,bytes),"Copied/unknown profile admitted");
        for(std::size_t n=0;n<bytes.size();++n){auto modified=bytes;modified[n]^=1;
            require(!pub::matches(p,module,modified),"Changed opcode/control flow/RIP operand admitted");}
        for(std::size_t n=0;n<bytes.size();++n)require(!pub::matches(p,module,std::span{bytes}.first(n)),"Partial procedure admitted");
        require(!pub::matches(p,0,bytes)&&!pub::matches(p,UINTPTR_MAX-16,bytes),"Invalid/overflowing module admitted");
        require(!pub::matches(p,pub::unpublished-p->unavailable,bytes),"No-write sentinel could masquerade as the old marker");
    }
    require(!pub::published(0)&&!pub::published(UINT64_MAX)&&!pub::published(0x100000000ull+pub::unpublished),"Partial register result accepted");
}
int main(){shape_cases();std::thread a(invocation_cases),b(invocation_cases);invocation_cases();a.join();b.join();
    require(calls==3*64*4&&caught==3*64,"OWN invocation/unwind counts");
    std::cout<<checks<<" checks; 960 OWN raw invocations, 192 actual C++ unwinds, three threads. "
        <<"Instruction-shape inputs fabricated; OWN Windows ASM fixture; no Native Game, MinHook, GPU, SDK, OpenXR or Quest execution.\n";
}
