#include "modern_camera_history.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace h=w3vr::camera_history;
namespace pose=w3vr::modern_camera_pose;
namespace ts=w3vr::temporal_source;
using C=w3vr::engine_camera::TemporalContract;
const C* contract=&w3vr::engine_camera::remastered_1048522;
unsigned checks{},builder_calls{},fail_at{},failure_kind{};
std::array<w3vr::engine_camera::TemporalInputs,4> received{};
void require(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
void setfloat(h::Prefix& p,std::size_t off,float v){std::memcpy(p.data()+off,&v,4);}
h::Prefix prefix(float marker){
    std::array<float,14> f{marker,2,3,0,4,5,6,75,100,200,1.7f,1,.1f,2000};
    h::Prefix p{};std::memcpy(p.data(),f.data(),p.size());
    const std::uint32_t opaque=0x7FAABCDE;std::memcpy(p.data()+0xC,&opaque,4);return p;
}
h::Inputs inputs(std::uint64_t pair){
    h::Inputs i{};i.identity={contract,7,pair,static_cast<std::int64_t>(pair*100)};
    i.raw_primary=prefix(static_cast<float>(pair));
    for(unsigned e=0;e<2;++e)for(unsigned c=0;c<2;++c){i.current[e][c]=prefix(static_cast<float>(pair*100+e*10+c));i.center[e][c]={.1f*e,.2f*c};}
    return i;
}
ts::Observation observed(const h::Prefix& previous,unsigned tick=1000000){
    ts::Observation o;o.kind=ts::Kind::built;o.clock={static_cast<std::int64_t>(tick),1000000.0,static_cast<float>(tick/1000000.0)};o.previous=previous;return o;
}
void* __fastcall own_builder(void* out,float time,const void* pos,const void* rot,float fov,float aspect,float nearp,float farp,float scale){
    const auto ordinal=builder_calls++;
    if(ordinal<4){auto& i=received[ordinal];std::memcpy(i.position.data(),pos,16);std::memcpy(i.rotation.data(),rot,16);
        i.time=time;i.fov=fov;i.aspect=aspect;i.near_plane=nearp;i.far_plane=farp;i.projection_scale=scale;}
    auto* bytes=static_cast<std::uint8_t*>(out);std::memset(bytes,0x5A,w3vr::engine_camera::record_bytes);bytes[0]=1;std::memcpy(bytes+4,&time,4);
    // OWN synthetic perspective formula, not copied Game matrices or code.
    std::array<float,16> projection{};
    const float y=1/std::tan(fov*3.14159265358979323846f/360.f);
    projection[0]=y/aspect*scale;projection[5]=y*scale;
    projection[10]=nearp/(nearp-farp);projection[11]=1;
    projection[14]=-farp*projection[10];
    std::memcpy(bytes+0x70,projection.data(),sizeof(projection));
    if(fail_at==ordinal+1){
        if(failure_kind==1){const float wrong=time+1;std::memcpy(bytes+4,&wrong,4);}
        else if(failure_kind==2)return static_cast<std::uint8_t*>(out)+1;
        else if(failure_kind==3){const float changed=.25f;std::memcpy(bytes+0x90,&changed,4);}
        else if(failure_kind==4){const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(bytes+0x70,&nan,4);}
        else bytes[0]=0;
    }
    return out;
}
void snapshot_checks(){
    std::vector<std::uint8_t> raw(0xF750);const auto* layout=w3vr::engine_camera_layout::selected(contract);
    for(unsigned c=0;c<2;++c){auto p=prefix(static_cast<float>(c));std::memcpy(raw.data()+layout->descriptor_cameras[c],p.data(),p.size());}
    pose::TrackingSample t;t.valid=true;t.display_time=100;
    for(auto& v:t.views)v.fov={-.8f,.8f,.8f,-.8f};
    t.views[0].pose={{0,0,0,1},{-.032f,0,0}};t.views[1].pose={{0,0,0,1},{.032f,0,0}};
    pose::PreparedPair prepared;
    require(pose::prepare_pair(contract,raw,t,{},prepared),"own pose pair fixture rejected");
    h::Inputs value{};require(h::snapshot(prepared,raw,t,{}, {contract,7,1,100},value),"four-prefix snapshot failed");
    for(unsigned e=0;e<2;++e)for(unsigned c=0;c<2;++c)
        require(!std::memcmp(value.current[e][c].data(),prepared.descriptors[e].data()+layout->descriptor_cameras[c],h::Prefix{}.size()),"eye/camera prefix exchanged");
    const auto saved=value;raw.assign(raw.size(),0xFF);for(auto& d:prepared.descriptors)d.assign(d.size(),0xEE);
    require(value==saved,"snapshot retained source buffers");
    h::Inputs unchanged=inputs(9);const auto before=unchanged;
    require(!h::snapshot(prepared,raw,t,{}, {contract,7,2,200},unchanged) && unchanged==before,"failed extraction published partial values");
}
void continuation_checks(){
    auto first=inputs(1),second=inputs(2);h::Bank bank;h::Plan p1,p2;
    require(bank.prepare(first,observed(prefix(0)),p1) && p1.reset==h::ResetReason::cold,"first history did not reset");
    require(bank.commit_cpu(p1,first.identity,true),"own CPU seed not committed");
    require(bank.prepare(second,observed(first.raw_primary,2000000),p2) && p2.reset==h::ResetReason::none && p2.previous==first.current,"four independent previous inputs lost");
    h::FourRecords result{};for(auto& e:result)for(auto& c:e)c.fill(0xCC);
    w3vr::engine_camera::TemporalRecord invalid{};builder_calls=0;fail_at=0;
    require(h::build_records(p2,&own_builder,invalid,result) && builder_calls==4,"did not build exactly four current-time records");
    for(unsigned e=0;e<2;++e)for(unsigned c=0;c<2;++c){const auto k=e*2+c;float marker{},expected{};
        std::memcpy(&marker,received[k].position.data(),4);std::memcpy(&expected,first.current[e][c].data(),4);
        require(marker==expected && received[k].time==2 && received[k].fov==75 && received[k].aspect==1.7f && received[k].near_plane==.1f && received[k].far_plane==2000 && received[k].projection_scale==1,"builder received wrong eye, camera, lens or NOW time");}
    for(unsigned e=0;e<2;++e)for(unsigned c=0;c<2;++c){std::array<float,2> center{};
        std::memcpy(center.data(),result[e][c].data()+0x90,8);
        require(center==p2.inputs.center[e][c],"wrong HMD eye/internal camera center in fresh history");}
    for(unsigned mode=0;mode<5;++mode)for(unsigned bad=1;bad<=4;++bad){builder_calls=0;fail_at=bad;failure_kind=mode;auto untouched=result;for(auto& e:untouched)for(auto& c:e)c.fill(0xA5);auto before=untouched;
        require(!h::build_records(p2,&own_builder,invalid,untouched) && untouched==before && builder_calls==bad,"partial native-model result published or extra calls made");}
    fail_at=0;builder_calls=0;auto cold=p1;
    require(h::build_records(cold,nullptr,invalid,result) && builder_calls==0,"cold reset called a builder or invented native time");
    auto reset_observation=observed(first.raw_primary,2000000);reset_observation.kind=ts::Kind::reset;
    h::Bank reset_bank;h::Plan reset_plan;require(reset_bank.prepare(second,reset_observation,reset_plan) && reset_plan.reset==h::ResetReason::native_reset,"native invalid path gained continuity");
    require(h::build_records(reset_plan,&own_builder,invalid,result) && builder_calls==0,"native reset invoked constructor");
    require(!bank.commit_cpu(p2,second.identity,false),"failed CPU pair dispatch committed data");
    auto third=inputs(3);h::Plan p3;require(bank.prepare(third,observed(second.raw_primary,3000000),p3) && p3.reset==h::ResetReason::cold,"failure retained an incomplete eye pair");
}
void discontinuity_checks(){
    for(unsigned kind=0;kind<7;++kind){h::Bank b;auto a=inputs(1),next=inputs(2);h::Plan x,y;auto o=observed(a.raw_primary,2000000);
        require(b.prepare(a,observed(prefix(0)),x) && b.commit_cpu(x,a.identity,true),"reset fixture seed failed");
        auto reason=h::ResetReason::none;
        switch(kind){case 0:next.identity.generation++;reason=h::ResetReason::generation;break;
        case 1:o.previous[0]^=1;reason=h::ResetReason::source;break;
        case 2:next.rig[0]++;reason=h::ResetReason::rig;break;
        case 3:next.center[1][0][0]+=.2f;reason=h::ResetReason::lens;break;
        case 4:setfloat(next.current[0][1],0x30,.2f);reason=h::ResetReason::lens;break;
        case 5:o.clock={500000,1000000,.5f};reason=h::ResetReason::clock;break;
        case 6:o.kind=ts::Kind::reset;reason=h::ResetReason::native_reset;break;}
        require(b.prepare(next,o,y) && y.reset==reason,"continuity used across native/rig/lens/generation/clock change");
        for(auto& e:y.previous)for(auto& p:e)require(p==h::Prefix{},"reset leaked old eye input");
    }
    h::Bank b;auto a=inputs(1),next=inputs(2);h::Plan x,y;
    require(b.prepare(a,observed(prefix(0)),x) && b.commit_cpu(x,a.identity,true),"nonce fixture seed failed");
    require(b.prepare(next,observed(a.raw_primary,2000000),y),"nonce fixture second failed");
    require(!b.cancel(x.ticket) && !b.commit_cpu(x,a.identity,true),"old callback removed newer reservation");
    require(b.commit_cpu(y,next.identity,true),"old callback revoked current reservation");
    h::Plan unchanged{};unchanged.ticket=777;
    require(!b.prepare(a,observed(prefix(0)),unchanged) && unchanged.ticket==777,"old pair replay rewound history");
    auto third=inputs(3);require(b.prepare(third,observed(next.raw_primary,3000000),x) && x.reset==h::ResetReason::cold,"rejected entry kept old inputs live");
    auto altered=x;altered.inputs.current[1][1][4]^=1;
    require(!b.commit_cpu(altered,third.identity,true) && !b.commit_cpu(x,third.identity,true),"tampered or revoked reservation committed");
    for(unsigned e=0;e<2;++e)for(unsigned c=0;c<2;++c){auto bad=inputs(5);setfloat(bad.current[e][c],0x34,0);h::Bank empty;h::Plan out{};out.ticket=888;
        require(!empty.prepare(bad,observed(prefix(0)),out) && out.ticket==888,"one invalid camera published another eye");}
    h::Bank failed;h::Plan rejected{};auto broken=inputs(1);setfloat(broken.current[0][0],0x34,0);
    require(!failed.prepare(broken,observed(prefix(0)),rejected) &&
        !failed.prepare(inputs(1),observed(prefix(0)),rejected),"same rejected pair rearmed after its data changed");
    auto ct=*contract;auto bad=inputs(6);bad.identity.contract=&ct;require(!h::accepted(bad),"copied profile gained history authority");
    for(unsigned k=0;k<5;++k){auto id=inputs(7).identity;if(k==0)id.contract=nullptr;if(k==1)id.generation=0;if(k==2)id.pair=0;if(k==3)id.pair=UINT64_MAX;if(k==4)id.display_time=0;require(!h::accepted(id),"unknown input identity accepted");}
}
void observation_checks(){
    ts::Captured captured;ts::Candidate c{};c.contract=contract;c.descriptor=0x100000;c.owner=0x200000;c.director=0x300000;
    c.history=c.director+0x6D0;c.position=c.director+0xE0;c.rotation=c.position+0x10;c.thread=1;
    c.clock={1000000,1000000,1};c.time=1;c.fov=75;c.aspect=1.7f;c.near_plane=.1f;c.far_plane=2000;c.scale=1;c.prefix=prefix(11);
    w3vr::engine_camera::TemporalRecord record{};record[0]=1;std::memcpy(record.data()+4,&c.time,4);
    require(captured.capture(c,record,c.history,true),"built observation fixture rejected");
    ts::Observation out{};require(captured.observation(out) && out.kind==ts::Kind::built && out.previous==c.prefix && out.clock.time==c.time,"built source did not expose owned copied inputs/time");
    const auto before=out;c.prefix.fill(0xA5);require(out.previous==before.previous,"observation retained candidate pointer");
    captured.revoke();require(!captured.observation(out) && out.previous==before.previous,"revoked observation partially replaced output");
    ts::Captured reset;ts::ResetCandidate rc{contract,0x140000000,0x100000,0x200000,0x300000,0x3006D0,0x140000000+ts::reset_source,1,{1000000,1000000,1}};
    record[0]=0;require(reset.capture_reset(rc,record,record,rc.history,true) && reset.observation(out) && out.kind==ts::Kind::reset && out.previous==ts::Prefix{} && out.reset_record[0]==0,"reset source invented previous-input admission");
}
void projection_checks(){
    namespace hp=w3vr::camera_history_projection;
    w3vr::engine_camera::TemporalRecord fresh{};builder_calls=0;fail_at=0;
    auto p=prefix(8);w3vr::engine_camera::TemporalInputs source{};
    require(w3vr::engine_camera::read_temporal_inputs(p,3,source),"projection fixture input invalid");
    own_builder(fresh.data(),3,source.position.data(),source.rotation.data(),75,1.7f,.1f,2000,1);
    for(const auto center:std::array<std::array<float,2>,5>{{{0,0},{.27f,-.13f},{-.41f,.19f},{.33f,.24f},{-.22f,-.37f}}}){
        auto out=fresh;require(hp::center_fresh_record(contract,center,fresh,out),"own fresh perspective center rejected");
        for(std::size_t i=0;i<out.size();++i)if(i<0x90 || i>=0x98)
            require(out[i]==fresh[i],"centering changed view/depth/time/pose/padding");
        std::array<float,16> before{},after{};std::memcpy(before.data(),fresh.data()+0x70,64);std::memcpy(after.data(),out.data()+0x70,64);
        for(const auto point:std::array<std::array<float,4>,4>{{{.3f,-.2f,.2f,1},{-1,2,4,1},{5,-3,11,1},{2,1,100,1}}}){
            std::array<double,4> a{},b{};
            for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row){a[col]+=static_cast<double>(point[row])*before[row*4+col];b[col]+=static_cast<double>(point[row])*after[row*4+col];}
            // Independent clip-space check: divide AFTER multiplication.
            require(std::abs(b[0]/b[3]-a[0]/a[3]-center[0])<1e-6 &&
                std::abs(b[1]/b[3]-a[1]/a[3]-center[1])<1e-6 && a[2]==b[2] && a[3]==b[3],"NDC center/depth/W incorrect");
        }
        if(center[0]!=0 || center[1]!=0){auto unchanged=out;
            require(!hp::center_fresh_record(contract,center,out,out) && out==unchanged,"center accumulated on previous result");}
        auto alias=fresh;require(hp::center_fresh_record(contract,center,alias,alias) && alias==out,"owned input/output alias corrupted record");
    }
    for(unsigned field=0;field<16;++field){auto broken=fresh;const float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(broken.data()+0x70+field*4,&nan,4);auto out=fresh;
        require(!hp::center_fresh_record(contract,{.1f,.2f},broken,out) && out==fresh,"nonfinite projection published output");}
    for(unsigned field:{1u,2u,3u,4u,6u,7u,8u,9u,12u,13u,15u}){auto broken=fresh;const float v=.3f;std::memcpy(broken.data()+0x70+field*4,&v,4);auto out=fresh;
        require(!hp::center_fresh_record(contract,{.1f,.2f},broken,out) && out==fresh,"unsupported fresh matrix shape accepted");}
    auto copied=*contract;
    for(const C* wrong:std::array<const C*,4>{nullptr,&copied,&w3vr::engine_camera::legacy_404,&w3vr::engine_camera::remastered_500c}){auto out=fresh;
        require(!hp::center_fresh_record(wrong,{.1f,.2f},fresh,out) && out==fresh,"unknown/copied/other-version profile accepted");}
    for(unsigned kind=0;kind<7;++kind){auto broken=fresh;auto center=std::array<float,2>{.1f,.2f};
        if(kind==0)broken[0]=0;else if(kind==1)broken[0]=2;
        else if(kind==2){const float v=0;std::memcpy(broken.data()+0x9C,&v,4);}
        else if(kind==3){const float v=-1;std::memcpy(broken.data()+0x70,&v,4);}
        else if(kind==4){const float v=std::numeric_limits<float>::infinity();std::memcpy(broken.data()+4,&v,4);}
        else center[kind-5]=std::numeric_limits<float>::quiet_NaN();
        auto out=fresh;require(!hp::center_fresh_record(contract,center,broken,out) && out==fresh,"invalid/reset/orthographic/nonfinite center accepted");}
}
int main(){projection_checks();observation_checks();snapshot_checks();continuation_checks();discontinuity_checks();std::printf("PASS four camera input histories: %u checks; owned snapshots, atomic pairs, four own constructor results and failures, perspective centers/NDC/depth, reset and stale callbacks.\n",checks);std::puts("GAME=false NATIVE_BUILDER_ADMISSION=false SDK_INIT=false GPU=false OPENXR_RUNTIME=false QUEST=false VR_FUNCTIONAL=false");}
