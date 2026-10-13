#include "modern_factory_clock.h"
#include "modern_factory_clock_windows.h"
#include <cstdio>
#include <cstdlib>
#include <type_traits>
namespace clock_route=w3vr::factory_clock;
using namespace clock_route;
unsigned checks{};
void require(bool ok,const char* reason){++checks;if(!ok){std::printf("FAIL %s\n",reason);std::exit(1);}}
const auto* contract=&w3vr::engine_camera::remastered_1048522;
constexpr std::uintptr_t module=0x140000000,low=0x100000,high=0x180000,desc=0x108000,owner=0x200000;
Caller correct(){return {module+current.factory_return,module,module+current.fragment,desc-0x100,
    desc+0x80,owner,low,high,77};}
constexpr std::uintptr_t viewport=0x300000;
Values values(std::size_t klass=2){return {owner,module+current.primary_vtables[klass],123456789,1000000.0,
    {viewport,viewport,owner+0x58,module+current.camera_vtables[klass],module+current.camera_getters[klass]}};}
static_assert(!std::is_copy_constructible_v<BorrowedScope> && !std::is_move_constructible_v<BorrowedScope>);
static_assert(sizeof(Sample)==24); // scalars only; no game pointers.
int main(){
    require(selected(contract)==&current && !selected(nullptr) &&
        !selected(&w3vr::engine_camera::legacy_404) && !selected(&w3vr::engine_camera::remastered_500c),"unexamined caller gained profile");
    auto copied_contract=*contract;require(!selected(&copied_contract),"copied camera contract admitted");
    auto copied_profile=current;require(!known(&copied_profile),"copied clock profile admitted");
    for(std::size_t klass=0;klass<current.primary_vtables.size();++klass){
        BorrowedScope scope;auto v=values(klass);
        require(scope.begin(contract,&current,correct(),desc,v,true,77),"known normal observation rejected");
        Sample frozen{};require(scope.sample(77,frozen) && frozen.ticks==v.ticks && frozen.scale==v.scale &&
            frozen.time==static_cast<float>(static_cast<double>(v.ticks)/v.scale),"native signed/double/float conversion wrong");
        require(scope.revalidate(contract,v,true,77) && scope.sample(77,frozen),"unchanged synchronous observation rejected");
        auto changed=v;changed.ticks++;require(!scope.revalidate(contract,changed,true,77),"clock advanced but remained valid");
        require(!scope.revalidate(contract,v,true,77) && !scope.sample(77,frozen),"old values reopened revoked scope");
    }
    for(unsigned kind=0;kind<26;++kind){
        BorrowedScope scope;auto c=correct();auto v=values();auto* p=&current;const auto* ct=contract;
        bool pin=true;std::uint32_t tid=77;auto d=desc;
        switch(kind){
        case 0:ct=nullptr;break;case 1:ct=&copied_contract;break;case 2:ct=&w3vr::engine_camera::remastered_500c;break;
        case 3:p=&copied_profile;break;case 4:p=nullptr;break;case 5:pin=false;break;
        case 6:tid=0;break;case 7:tid=78;break;case 8:c.pc++;break;case 9:c.module+=0x1000;break;
        case 10:c.fragment=module+current.unwind.back().begin;break;case 11:c.sp=low-8;break;
        case 12:c.sp=high;break;case 13:c.sp++;break;case 14:c.rbp=0x7F;break;
        case 15:d++;break;case 16:c.stack_high=d+0xF74F;break;case 17:c.stack_low=d+1;break;
        case 18:c.rdi=0;break;case 19:c.rdi++;break;case 20:v.owner+=8;break;case 21:v.table+=8;break;
        case 22:v.scale=0;break;case 23:v.scale=-1;break;case 24:v.scale=NAN;break;case 25:v.scale=INFINITY;break;
        }
        require(!scope.begin(ct,p,c,d,v,pin,tid),"bad caller/module/stack/class/time admitted");
        Sample unchanged{1,2,3};require(!scope.sample(77,unchanged) && unchanged.ticks==1 && unchanged.scale==2 && unchanged.time==3,
            "rejected observation partially exposed time");
        require(!scope.begin(contract,&current,correct(),desc,values(),true,77),"rejected entry admitted after state changed");
    }
    for(unsigned kind=0;kind<7;++kind){
        BorrowedScope scope;require(scope.begin(contract,&current,correct(),desc,values(),true,77),"revalidation fixture rejected");
        auto v=values();auto* ct=contract;bool pin=true;auto tid=77u;
        if(kind==0)v.owner+=8;if(kind==1)v.table+=8;if(kind==2)v.scale*=2;if(kind==3)v.ticks++;
        if(kind==4)ct=&copied_contract;if(kind==5)pin=false;if(kind==6)tid++;
        require(!scope.revalidate(ct,v,pin,tid) && !scope.revalidate(contract,values(),true,77),"changed evidence did not revoke permanently");
    }
    // Bind descriptor -> viewport -> SECONDARY camera interface, not primary slot11.
    for(std::size_t klass=0;klass<3;++klass){
        for(unsigned kind=0;kind<13;++kind){
            auto v=values(klass);auto bad=v;
            switch(kind){
            case 0:bad.camera.descriptor_viewport=0;break;
            case 1:bad.camera.descriptor_viewport++;break;
            case 2:bad.camera.owner_viewport+=8;break;
            case 3:bad.camera.receiver=0;break;
            case 4:bad.camera.receiver=owner;break;
            case 5:bad.camera.receiver+=8;break;
            case 6:bad.camera.table=bad.table;break;
            case 7:bad.camera.table=module+current.camera_vtables[(klass+1)%3];break;
            case 8:bad.camera.getter=0;break;
            case 9:bad.camera.getter=module+current.camera_getters[(klass+1)%3];break;
            case 10:bad.camera.getter++;break;
            case 11:bad.owner=UINTPTR_MAX-0x57;break;
            case 12:bad.camera.table=0;break;
            }
            require(!camera_matches(&current,module,bad),"foreign viewport or mixed camera class admitted");
            BorrowedScope rejected;require(!rejected.begin(contract,&current,correct(),desc,bad,true,77),"bad camera admitted at entry");
            BorrowedScope valid;require(valid.begin(contract,&current,correct(),desc,v,true,77),"camera fixture rejected");
            require(valid.borrowed_descriptor()==desc,"original descriptor not retained for same CPU scope");
            require(!valid.revalidate(contract,bad,true,77) && !valid.revalidate(contract,v,true,77),"camera change did not revoke permanently");
        }
    }
    require(!camera_matches(&copied_profile,module,values()) && !camera_matches(&current,0,values()),"copied profile or absent module admitted camera");
    // The Windows reader touches only OWN reserved memory here, never a Game
    // object or SDK. It checks fault handling and output atomicity, not Native life.
    auto* arena=static_cast<std::byte*>(VirtualAlloc(nullptr,current.image_bytes,MEM_RESERVE,PAGE_READWRITE));
    require(arena!=nullptr,"own memory reservation failed");
    const auto fake_module=reinterpret_cast<std::uintptr_t>(arena);
    auto commit=[&](std::uintptr_t rva){
        const auto page=rva&~std::uintptr_t(0xFFF);
        require(VirtualAlloc(arena+page,0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"own page commit failed");
    };
    commit(current.clock_global);commit(current.scale_global);commit(current.camera_vtables[2]);
    alignas(8) std::array<std::byte,0x148> game{};
    alignas(8) std::array<std::uintptr_t,2> own_viewport{},own_descriptor{};
    const auto own_owner=reinterpret_cast<std::uintptr_t>(game.data());
    const auto own_desc=reinterpret_cast<std::uintptr_t>(own_descriptor.data());
    const auto own_vp=reinterpret_cast<std::uintptr_t>(own_viewport.data());
    auto store=[&](std::uintptr_t address,const auto& value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));};
    Values expected{own_owner,fake_module+current.primary_vtables[2],123456789,1000000.0,
        {own_vp,own_vp,own_owner+0x58,fake_module+current.camera_vtables[2],fake_module+current.camera_getters[2]}};
    store(fake_module+current.clock_global,own_owner);store(fake_module+current.scale_global,expected.scale);
    store(own_owner,expected.table);store(own_owner+0x140,expected.ticks);store(own_owner+0x100,own_vp);
    store(own_owner+0x58,expected.camera.table);store(expected.camera.table+11*8,expected.camera.getter);
    own_viewport[1]=expected.camera.receiver;own_descriptor[0]=own_vp;
    Values read{};require(clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,read) &&
        read.camera==expected.camera && read.ticks==expected.ticks,"own protected reader failed exact chain");
    auto output=read;
    own_viewport[1]=own_owner;require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"wrong receiver published partial output");
    own_viewport[1]=expected.camera.receiver;own_descriptor[0]=1;
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"misaligned viewport admitted");
    own_descriptor[0]=own_vp;
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,fake_module+0x1000,output) && output.camera==read.camera,"inaccessible descriptor admitted or output changed");
    store(own_owner+0x58,std::uintptr_t(1));
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"unknown secondary table dereferenced");
    store(own_owner+0x58,expected.camera.table);store(expected.camera.table+11*8,std::uintptr_t(0));
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"replaced getter admitted");
    require(VirtualFree(arena,0,MEM_RELEASE)!=0,"own reservation not freed");
    BorrowedScope scope;require(scope.begin(contract,&current,correct(),desc,values(),true,77),"final scope rejected");
    Sample s{1,2,3};require(!scope.sample(78,s) && s.time==3,"foreign thread borrowed time");
    scope.revoke();require(!scope.sample(77,s),"revoked scope exposed scalar");
    std::uintptr_t end=11;require(!offset(UINTPTR_MAX-1,2,end) && end==11 && !range(UINTPTR_MAX-10,20,1,UINTPTR_MAX),"wrapped address accepted");
    require(!range(desc,0,low,high) && !range(desc,0xF750,high,low),"empty/inverted stack range accepted");
    auto v=values();v.ticks=INT64_MIN;Sample negative{};
    require(sample_from(v,negative) && negative.time==static_cast<float>(static_cast<double>(INT64_MIN)/v.scale),"signed negative tick lost");
    v.scale=std::numeric_limits<double>::min();Sample unchanged{1,2,3};
    require(!sample_from(v,unchanged) && unchanged.time==3,"overflow time partially published");
    require(!clock_route::windows::module_matches(&current,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))) &&
        !clock_route::windows::module_matches(&copied_profile,module),"own or copied module got Game proof");
    std::printf("PASS factory clock borrowed scope: %u checks; CPU observations fabricated, Windows own module rejected.\n",checks);
    std::puts("GAME_CLOCK_RUNTIME=false GAME_LIFETIME=false SDK_INIT=false GPU=false VR_FUNCTIONAL=false");
}
