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
Values values(){return {owner,module+current.primary_vtables[2],123456789,1000000.0};}
static_assert(!std::is_copy_constructible_v<BorrowedScope> && !std::is_move_constructible_v<BorrowedScope>);
static_assert(sizeof(Sample)==24); // scalars only; no game pointers.
int main(){
    require(selected(contract)==&current && !selected(nullptr) &&
        !selected(&w3vr::engine_camera::legacy_404) && !selected(&w3vr::engine_camera::remastered_500c),"unexamined caller gained profile");
    auto copied_contract=*contract;require(!selected(&copied_contract),"copied camera contract admitted");
    auto copied_profile=current;require(!known(&copied_profile),"copied clock profile admitted");
    for(auto table:current.primary_vtables){
        BorrowedScope scope;auto v=values();v.table=module+table;
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
