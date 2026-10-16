#include "modern_factory_clock.h"
#include "modern_factory_clock_windows.h"
#include "modern_temporal_source.h"
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
static_assert(!std::is_copy_constructible_v<w3vr::temporal_source::Captured> && !std::is_move_constructible_v<w3vr::temporal_source::Captured>);
static_assert(sizeof(Sample)==24); // scalars only; no game pointers.
unsigned own_builder_calls{};
void* own_source_builder(void* dst,float time,const void* position,const void* rotation,
    float fov,float aspect,float near_plane,float far_plane,float scale){
    ++own_builder_calls;require(std::isfinite(time) && fov==75 && aspect==1.5f && near_plane==0.1f &&
        far_plane==1000 && scale==1,"own nine-argument builder received different inputs");
    auto* bytes=static_cast<std::uint8_t*>(dst);std::memset(bytes,0x41,w3vr::engine_camera::record_bytes);
    bytes[0]=1;std::memcpy(bytes+4,&time,4);std::memcpy(bytes+8,&fov,4);
    std::memcpy(bytes+0x10,position,16);std::memcpy(bytes+0x20,rotation,12);return dst;
}
void source_receipt_checks(){
    namespace ts=w3vr::temporal_source;
    auto ctor=correct();ctor.pc=module+descriptor_constructor_return;
    require(!caller_matches(&current,ctor,desc,77) && caller_matches(&current,ctor,desc,77,CallSite::descriptor_constructor),"constructor and factory call sites mixed");
    require(!caller_matches(&current,correct(),desc,77,CallSite::descriptor_constructor) &&
        !caller_matches(&current,ctor,desc,77,static_cast<CallSite>(99)),"wrong call site accepted");
    BorrowedScope ctor_scope;require(ctor_scope.begin(contract,&current,ctor,desc,values(),true,77,CallSite::descriptor_constructor),"explicit constructor scope rejected");
    BorrowedScope default_scope;require(!default_scope.begin(contract,&current,ctor,desc,values(),true,77),"factory default widened to constructor");
    ts::Candidate c;c.contract=contract;c.descriptor=desc;c.owner=owner;c.director=0x500000;
    c.history=c.director+0x6D0;c.position=c.director+0xE0;c.rotation=c.position+0x10;c.thread=77;
    require(ctor_scope.sample(77,c.clock),"constructor clock sample unavailable");
    c.time=c.clock.time;c.fov=75;c.aspect=1.5f;c.near_plane=0.1f;c.far_plane=1000;c.scale=1;
    std::array<float,14> input{1,2,3,0,4,5,6,75,0,0,1.5f,1,0.1f,1000};
    std::memcpy(c.prefix.data(),input.data(),sizeof(input));
    const std::uint32_t opaque=0xFFFFFFFF;std::memcpy(c.prefix.data()+0xC,&opaque,4);
    require(ts::accepted(c),"correct previous-input candidate rejected");
    w3vr::engine_camera::TemporalRecord record{};
    require(w3vr::engine_camera::build_record(c.prefix,c.time,&own_source_builder,record) && own_builder_calls==1,"own builder was not called once with staged inputs");
    ts::Captured captured;require(captured.capture(c,record,c.history,true),"observed model result rejected");
    ts::Prefix prefix{};require(captured.previous_inputs(prefix) && prefix==c.prefix,"previous input bits changed or not copied");
    require(captured.matches(contract,desc,owner,77,c.clock,record,true),"exact factory record rejected");
    for(unsigned kind=0;kind<17;++kind){
        auto bad=c;switch(kind){
        case 0:bad.contract=nullptr;break;case 1:bad.contract=&w3vr::engine_camera::remastered_500c;break;
        case 2:bad.descriptor=0;break;case 3:bad.owner=0;break;case 4:bad.director=0;break;
        case 5:bad.director++;break;case 6:bad.history+=8;break;case 7:bad.position+=4;break;
        case 8:bad.rotation+=4;break;case 9:bad.thread=0;break;case 10:bad.clock.ticks+=1000000;break;
        case 11:bad.time=std::nextafter(bad.time,INFINITY);break;case 12:bad.fov++;break;
        case 13:bad.aspect++;break;case 14:bad.near_plane*=2;break;case 15:bad.far_plane++;break;case 16:bad.scale*=2;break;
        }
        require(!ts::accepted(bad),"changed native arguments accepted");
        ts::Captured rejected;require(!rejected.capture(bad,record,c.history,true),"bad candidate captured");
        ts::Prefix unchanged{};unchanged.fill(0xA5);
        require(!rejected.previous_inputs(unchanged) && unchanged[0]==0xA5,"failed capture published partial prefix");
        require(!rejected.capture(c,record,c.history,true),"rejected capture rearmed in same producer");
    }
    for(unsigned kind=0;kind<4;++kind){
        ts::Captured rejected;auto wrong=record;auto result=c.history;bool live=true;
        if(kind==0)result++;if(kind==1)live=false;if(kind==2)wrong[0]=0;if(kind==3)wrong[4]^=1;
        require(!rejected.capture(c,wrong,result,live),"wrong output/return/liveness admitted");
    }
    for(std::size_t i=0;i<record.size();++i){
        ts::Captured checked;require(checked.capture(c,record,c.history,true),"field comparison fixture rejected");
        auto changed=record;changed[i]^=1;
        const bool padding=(i>=1 && i<4) || (i>=0xC && i<0x10) || (i>=0x2C && i<0x30);
        require(checked.matches(contract,desc,owner,77,c.clock,changed,true)==padding,"defined native record field or padding classified incorrectly");
    }
    for(unsigned kind=0;kind<6;++kind){
        ts::Captured checked;require(checked.capture(c,record,c.history,true),"factory identity fixture rejected");
        auto sample=c.clock;auto d=desc,o=owner;auto tid=77u;bool live=true;std::span<const std::uint8_t> bytes=record;
        if(kind==0)sample.ticks++;if(kind==1)d+=8;if(kind==2)o+=8;if(kind==3)tid++;if(kind==4)live=false;
        if(kind==5)bytes=bytes.first(bytes.size()-1);
        require(!checked.matches(contract,d,o,tid,sample,bytes,live) &&
            !checked.matches(contract,desc,owner,77,c.clock,record,true),"foreign factory or failed check rearmed receipt");
    }
    require(!captured.capture(c,record,c.history,true) && !captured.previous_inputs(prefix),"second same-producer observation silently replaced first");
    require(own_builder_calls==1,"receipt checks called builder again");
}
void producer_source_receipt_checks(){
    namespace ts=w3vr::temporal_source;
    ts::Candidate c;c.contract=contract;c.owner=owner;c.director=0x500000;
    c.history=c.director+0x6D0;c.position=c.director+0xE0;c.rotation=c.position+0x10;c.thread=77;
    c.site=ts::SourceSite::producer_update;c.module=module;c.caller=module+ts::producer_update_return;
    require(sample_from(values(),c.clock),"early owner clock fixture invalid");
    c.time=c.clock.time;c.fov=75;c.aspect=1.5f;c.near_plane=0.1f;c.far_plane=1000;c.scale=1;
    const std::array<float,14> input{1,2,3,0,4,5,6,75,0,0,1.5f,1,0.1f,1000};
    std::memcpy(c.prefix.data(),input.data(),sizeof(input));
    w3vr::engine_camera::TemporalRecord record{};const auto count=own_builder_calls;
    require(w3vr::engine_camera::build_record(c.prefix,c.time,&own_source_builder,record),"own early output fixture failed");
    ts::Captured captured;require(captured.capture(c,record,c.history,true),"early result not staged before descriptor exists");
    ts::Prefix prefix{};prefix.fill(0xA5);ts::Observation observation{};observation.kind=ts::Kind::reset;
    auto unchanged=record;unchanged[0]=0xA5;
    require(!captured.previous_inputs(prefix)&&prefix[0]==0xA5 && !captured.built_record(unchanged)&&unchanged[0]==0xA5 &&
        !captured.observation(observation)&&observation.kind==ts::Kind::reset,"unbound source published authority/partial output");
    require(captured.matches(contract,desc,owner,77,c.clock,record,true),"first real matching descriptor did not bind");
    require(captured.previous_inputs(prefix)&&prefix==c.prefix && captured.observation(observation)&&observation.kind==ts::Kind::built,
        "bound source did not publish copied inputs");
    require(captured.matches(contract,desc,owner,77,c.clock,record,true),"same bound descriptor rejected");
    require(!captured.matches(contract,desc+8,owner,77,c.clock,record,true) &&
        !captured.matches(contract,desc,owner,77,c.clock,record,true),"bound source retargeted or rearmed");
    for(unsigned k=0;k<12;++k){
        auto bad=c;if(k==0)bad.descriptor=desc;if(k==1)bad.module=0;if(k==2)bad.module++;
        if(k==3)bad.caller++;if(k==4)bad.caller=module+ts::wrapper_return;if(k==5)bad.site=static_cast<ts::SourceSite>(99);
        if(k==6)bad.contract=&w3vr::engine_camera::remastered_500c;if(k==7)bad.owner=0;if(k==8)bad.thread=0;
        if(k==9)bad.position++;if(k==10)bad.time=std::nextafter(bad.time,INFINITY);if(k==11)bad.clock.ticks+=1000000;
        ts::Captured rejected;require(!ts::accepted(bad)&&!rejected.capture(bad,record,c.history,true),"foreign early source accepted");
        require(!rejected.capture(c,record,c.history,true),"failed early capture rearmed");
    }
    for(unsigned k=0;k<10;++k){
        ts::Captured fixture;require(fixture.capture(c,record,c.history,true),"early late-bind fixture failed");
        auto d=desc,o=owner;auto thread=77u;auto sample=c.clock;auto data=record;auto target_contract=contract;bool live=true;
        if(k==0)d=0;if(k==1)d++;if(k==2)o++;if(k==3)thread++;if(k==4)sample.ticks++;
        if(k==5)sample.scale*=2;if(k==6)sample.time=std::nextafter(sample.time,INFINITY);if(k==7)data[0]=0;
        if(k==8)target_contract=&w3vr::engine_camera::remastered_500c;if(k==9)live=false;
        require(!fixture.matches(target_contract,d,o,thread,sample,data,live) &&
            !fixture.matches(contract,desc,owner,77,c.clock,record,true),"bad first binding gained a second attempt");
    }
    ts::Captured changed_record;require(changed_record.capture(c,record,c.history,true),"record mismatch fixture capture failed");
    auto changed=record;changed[0x30]^=1;
    require(!changed_record.matches(contract,desc,owner,77,c.clock,changed,true) &&
        !changed_record.previous_inputs(prefix),"different computed history admitted");
    require(own_builder_calls==count+1,"source binding called constructor again");
}
void reset_receipt_checks(){
    namespace ts=w3vr::temporal_source;
    ts::ResetCandidate c{contract,module,desc,owner,0x500000,0x5006D0,module+ts::reset_source,77};
    require(sample_from(values(),c.clock),"reset producer identity unavailable");
    w3vr::engine_camera::TemporalRecord canonical{};
    for(std::size_t i=1;i<canonical.size();++i)canonical[i]=static_cast<std::uint8_t>(i*19);
    auto copied=canonical;
    ts::Captured reset;
    require(reset.capture_reset(c,canonical,copied,c.history,true) && reset.reset_observed(),"exact own invalid copy not captured");
    ts::Prefix unchanged{};unchanged.fill(0xA5);
    require(!reset.previous_inputs(unchanged) && unchanged[0]==0xA5,"invalid path invented previous-input/time proof");
    require(reset.matches(contract,desc,owner,77,c.clock,copied,true),"own invalid receipt not matched");
    for(unsigned k=0;k<15;++k){
        auto bad=c;auto src=canonical,dst=copied;auto result=c.history;bool live=true;
        if(k==0)bad.contract=nullptr;if(k==1)bad.module++;if(k==2)bad.source++;
        if(k==3)bad.director++;if(k==4)bad.history++;if(k==5)bad.descriptor=0;
        if(k==6)bad.owner=0;if(k==7)bad.thread=0;if(k==8)src[0]=1;if(k==9)result++;if(k==10)live=false;
        if(k==11)bad.director=UINTPTR_MAX-7;if(k==12)bad.module=UINTPTR_MAX&~std::uintptr_t(0xFFF);
        if(k==13)bad.contract=&w3vr::engine_camera::remastered_500c;if(k==14)dst[0]=1;
        ts::Captured rejected;
        require(!rejected.capture_reset(bad,src,dst,result,live) && !rejected.reset_observed(),"foreign invalid copy admitted");
        require(!rejected.capture_reset(c,canonical,copied,c.history,true),"invalid failure rearmed");
    }
    for(std::size_t i=0;i<copied.size();++i){
        ts::Captured fixture;auto changed=copied;changed[i]^=1;
        const bool padding=(i>=1 && i<4) || (i>=0xC && i<0x10) || (i>=0x2C && i<0x30);
        require(fixture.capture_reset(c,canonical,changed,c.history,true)==padding,"invalid fields/padding compared incorrectly");
    }
    auto altered=c.clock;altered.ticks++;
    require(!reset.matches(contract,desc,owner,77,altered,copied,true) &&
        !reset.matches(contract,desc,owner,77,c.clock,copied,true),"revoked reset became valid again");
}
void producer_reset_receipt_checks(){
    namespace ts=w3vr::temporal_source;
    ts::ResetCandidate c{contract,module,0,owner,0x500000,0x5006D0,module+ts::reset_source,77};
    c.site=ts::SourceSite::producer_update;c.caller=module+ts::producer_reset_return;
    require(sample_from(values(),c.clock),"own early reset identity unavailable");
    w3vr::engine_camera::TemporalRecord canonical{};
    for(std::size_t i=1;i<canonical.size();++i)canonical[i]=static_cast<std::uint8_t>(i*17+3);
    const auto copied=canonical;const auto before=own_builder_calls;
    ts::Captured pending;require(pending.capture_reset(c,canonical,copied,c.history,true)&&pending.reset_observed(),"early invalid copy not staged");
    ts::Observation output{};output.kind=ts::Kind::built;ts::Prefix prefix{};prefix.fill(0xA5);
    auto record=copied;record[0]=0xA5;
    require(!pending.observation(output)&&output.kind==ts::Kind::built && !pending.previous_inputs(prefix)&&prefix[0]==0xA5 &&
        !pending.built_record(record)&&record[0]==0xA5,"pending reset published history or constructor time");
    require(pending.matches(contract,desc,owner,77,c.clock,copied,true)&&pending.observation(output)&&output.kind==ts::Kind::reset &&
        output.reset_record==copied,"first real descriptor did not receive exact own invalid values");
    require(!pending.previous_inputs(prefix)&&!pending.built_record(record),"bound reset promoted itself to previous inputs or valid record");
    require(!pending.matches(contract,desc+8,owner,77,c.clock,copied,true)&&
        !pending.matches(contract,desc,owner,77,c.clock,copied,true),"bound reset retargeted or rearmed");
    for(unsigned k=0;k<14;++k){
        auto bad=c;if(k==0)bad.descriptor=desc;if(k==1)bad.module=0;if(k==2)bad.module++;
        if(k==3)bad.caller++;if(k==4)bad.caller=module+ts::reset_return;if(k==5)bad.site=static_cast<ts::SourceSite>(99);
        if(k==6)bad.source++;if(k==7)bad.history++;if(k==8)bad.owner=0;if(k==9)bad.thread=0;
        if(k==10)bad.contract=&w3vr::engine_camera::remastered_500c;if(k==11)bad.clock.time=std::nextafter(bad.clock.time,INFINITY);
        if(k==12)bad.director=UINTPTR_MAX-7;if(k==13)bad.module=UINTPTR_MAX&~std::uintptr_t(0xFFF);
        ts::Captured rejected;require(!ts::accepted(bad)&&!rejected.capture_reset(bad,canonical,copied,c.history,true),"foreign early reset accepted");
        require(!rejected.capture_reset(c,canonical,copied,c.history,true),"early reset failed attempt rearmed");
    }
    for(unsigned k=0;k<8;++k){
        ts::Captured fixture;require(fixture.capture_reset(c,canonical,copied,c.history,true),"early reset binding fixture failed");
        auto d=desc,o=owner;auto tid=77u;auto clock=c.clock;auto changed=copied;bool live=true;
        if(k==0)d=0;if(k==1)d++;if(k==2)o++;if(k==3)tid++;if(k==4)clock.ticks++;
        if(k==5)clock.scale*=2;if(k==6)changed[0x10]^=1;if(k==7)live=false;
        require(!fixture.matches(contract,d,o,tid,clock,changed,live)&&
            !fixture.matches(contract,desc,owner,77,c.clock,copied,true),"invalid binding got a second opportunity");
    }
    ts::Captured changed_valid;auto valid=canonical;valid[0]=1;
    require(!changed_valid.capture_reset(c,valid,valid,c.history,true),"valid record promoted to canonical reset");
    ts::Captured bad_return;require(!bad_return.capture_reset(c,canonical,copied,c.history+8,true),"wrong leaf result accepted");
    for(std::size_t i=0;i<copied.size();++i){
        ts::Captured fields;auto changed=copied;changed[i]^=1;
        const bool padding=(i>=1&&i<4)||(i>=0xC&&i<0x10)||(i>=0x2C&&i<0x30);
        require(fields.capture_reset(c,canonical,changed,c.history,true)==padding,"early invalid field/padding copy wrong");
        if(padding)require(fields.matches(contract,desc,owner,77,c.clock,changed,true),"native padding required source equality");
    }
    require(own_builder_calls==before,"early reset/binding called a constructor");
}
template<std::size_t N> std::uint32_t read32(const std::array<std::uint8_t,N>& bytes,std::size_t at){
    std::uint32_t v{};std::memcpy(&v,bytes.data()+at,4);return v;
}
// WORLD branch: factory is entered from builder 022C0820, captured at the
// producer frame. These fixtures are CPU only; pins come from the examined exe.
void world_site_checks(){
    const auto& w=current.world;
    std::uintptr_t pc{},fragment{};CallSite site{};
    require(factory_site(&current,module,module+current.factory_return,site) && site==CallSite::factory &&
        factory_site(&current,module,module+w.factory_return,site) && site==CallSite::world_factory,"factory immediate callers not classified");
    require(!factory_site(&current,module,module+w.factory_anchor,site) && !factory_site(&current,module,module+w.factory_return+1,site) &&
        !factory_site(nullptr,module,module+w.factory_return,site) && !factory_site(&current,module,module-1,site),"foreign factory caller classified");
    require(site_entry(&current,CallSite::world_factory,pc) && pc==w.factory_return &&
        site_anchor(&current,CallSite::world_factory,pc,fragment) && pc==w.factory_anchor && fragment==w.fragment &&
        site_entry(&current,CallSite::world_descriptor_constructor,pc) && pc==w.descriptor_anchor &&
        site_entry(&current,CallSite::factory,pc) && pc==current.factory_return &&
        site_entry(&current,CallSite::descriptor_constructor,pc) && pc==descriptor_constructor_return &&
        !site_anchor(&current,static_cast<CallSite>(99),pc,fragment),"site entry/anchor table wrong");
    require(world_site(CallSite::world_factory) && world_site(CallSite::world_descriptor_constructor) &&
        !world_site(CallSite::factory) && !world_site(CallSite::descriptor_constructor),"world site classification wrong");
    // Static consistency of the examined code/xdata pins with the anchors.
    require(w.pins[5].rva+world_builder_call_pin.size()==w.factory_anchor &&
        w.factory_anchor+read32(world_builder_call_pin,world_builder_call_pin.size()-4)==w.builder_unwind[1].begin,
        "builder call pin does not return to the producer anchor or target the builder");
    require(w.pins[6].rva+31==w.factory_return && world_factory_call_pin[28]==0x41 && world_factory_call_pin[29]==0xFF &&
        world_factory_call_pin[30]==0xD1,"factory call pin does not end at the immediate return");
    require(w.pins[3].rva+world_descriptor_pin.size()==w.descriptor_anchor,"descriptor vt[0x170] pin not at its anchor");
    require(w.anchor_unwind[0].begin==w.fragment && w.anchor_unwind[0].data==w.pins[0].rva &&
        read32(world_fragment_unwind_pin,8)==w.anchor_unwind[1].begin && read32(world_fragment_unwind_pin,12)==w.anchor_unwind[1].end &&
        read32(world_fragment_unwind_pin,16)==w.anchor_unwind[1].data,"world fragment xdata does not chain to the producer");
    for(std::size_t i=1;i<w.anchor_unwind.size();++i){
        const auto& a=w.anchor_unwind[i];const auto& b=current.unwind[i];
        require(a.begin==b.begin && a.end==b.end && a.data==b.data && a.slots==b.slots && a.flags==b.flags,"world chain diverged from the no-world producer chain");
    }
    require(w.builder_unwind[0].begin<=w.factory_return && w.factory_return<w.builder_unwind[0].end &&
        read32(world_builder_fragment_unwind_pin,4)==w.builder_unwind[1].begin && read32(world_builder_fragment_unwind_pin,12)==w.builder_unwind[1].data,
        "immediate return outside the builder fragment or chain broken");
    require(w.fragment<=w.factory_anchor && w.factory_anchor<w.anchor_unwind[0].end &&
        w.fragment<=w.descriptor_anchor && w.descriptor_anchor<w.anchor_unwind[0].end,"world anchors outside the producer fragment");
    // Caller semantics: same RBP-0x80 descriptor and CGame RDI as no-world.
    auto world=correct();world.pc=module+w.factory_anchor;world.fragment=module+w.fragment;
    auto ctor=world;ctor.pc=module+w.descriptor_anchor;
    require(caller_matches(&current,world,desc,77,CallSite::world_factory) && !caller_matches(&current,world,desc,77) &&
        !caller_matches(&current,correct(),desc,77,CallSite::world_factory) &&
        caller_matches(&current,ctor,desc,77,CallSite::world_descriptor_constructor) &&
        !caller_matches(&current,ctor,desc,77,CallSite::world_factory) &&
        !caller_matches(&current,world,desc,77,CallSite::world_descriptor_constructor),"world call sites mixed with each other or no-world");
    for(unsigned kind=0;kind<8;++kind){
        auto bad=world;auto d=desc;auto tid=77u;
        if(kind==0)bad.pc=module+w.factory_return;if(kind==1)bad.fragment=module+current.fragment;if(kind==2)d+=8;
        if(kind==3)bad.rdi=0;if(kind==4)bad.rbp=0x7F;if(kind==5)tid=78;if(kind==6)bad.sp=high;if(kind==7)bad.rdi++;
        require(!caller_matches(&current,bad,d,tid,CallSite::world_factory),"bad world caller admitted");
    }
    BorrowedScope scope;require(scope.begin(contract,&current,world,desc,values(),true,77,CallSite::world_factory) &&
        scope.site()==CallSite::world_factory && scope.borrowed_descriptor()==desc,"world factory scope rejected or lost its site");
    Sample s{};require(scope.sample(77,s) && scope.revalidate(contract,values(),true,77),"world scope sample/revalidate failed");
    BorrowedScope wrong;require(!wrong.begin(contract,&current,world,desc,values(),true,77),"world caller admitted as no-world factory");
    BorrowedScope ctor_scope;require(ctor_scope.begin(contract,&current,ctor,desc,values(),true,77,CallSite::world_descriptor_constructor) &&
        ctor_scope.site()==CallSite::world_descriptor_constructor,"world constructor scope rejected");
    auto copied=current;BorrowedScope copied_scope;
    require(!copied_scope.begin(contract,&copied,world,desc,values(),true,77,CallSite::world_factory),"copied profile admitted world site");
    require(!clock_route::windows::world_matches(&current,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))) &&
        !clock_route::windows::site_matches(&current,reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)),CallSite::world_factory),
        "own module got world Game proof");
}
int main(){
    world_site_checks();
    source_receipt_checks();
    producer_source_receipt_checks();
    reset_receipt_checks();
    producer_reset_receipt_checks();
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
    clock_route::windows::OwnerObservation owner_read{};Sample owner_sample{};
    require(clock_route::windows::read_owner(&current,fake_module,own_owner,owner_read,owner_sample) &&
        owner_read.owner==own_owner && owner_read.viewport==expected.camera.owner_viewport &&
        owner_read.receiver==expected.camera.receiver && owner_sample.ticks==expected.ticks,
        "early reader required/invented descriptor instead of own paired owner clock");
    const auto saved_owner=owner_read;const auto saved_sample=owner_sample;
    require(!clock_route::windows::read_owner(&current,fake_module,own_owner+8,owner_read,owner_sample) &&
        owner_read==saved_owner && owner_sample.ticks==saved_sample.ticks,"wrong early owner published partial output");
    auto unknown_profile=current;
    require(!clock_route::windows::read_owner(&unknown_profile,fake_module,own_owner,owner_read,owner_sample),"copied early profile accepted");
    Values read{};require(clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,read) &&
        read.camera==expected.camera && read.ticks==expected.ticks,"own protected reader failed exact chain");
    auto output=read;
    own_viewport[1]=own_owner;
    require(!clock_route::windows::read_owner(&current,fake_module,own_owner,owner_read,owner_sample)&&owner_read==saved_owner,"early wrong receiver admitted");
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"wrong receiver published partial output");
    own_viewport[1]=expected.camera.receiver;own_descriptor[0]=1;
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"misaligned viewport admitted");
    own_descriptor[0]=own_vp;
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,fake_module+0x1000,output) && output.camera==read.camera,"inaccessible descriptor admitted or output changed");
    store(own_owner+0x58,std::uintptr_t(1));
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"unknown secondary table dereferenced");
    store(own_owner+0x58,expected.camera.table);store(expected.camera.table+11*8,std::uintptr_t(0));
    require(!clock_route::windows::read_values(&current,fake_module,own_owner,own_desc,output) && output.camera==read.camera,"replaced getter admitted");
    require(!clock_route::windows::read_owner(&current,fake_module,own_owner,owner_read,owner_sample)&&owner_read==saved_owner,"early replaced getter admitted");
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
