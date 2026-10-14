#include "modern_camera_history_materialization.h"
#include <cstdio>
#include <cstdlib>
namespace h=w3vr::camera_history;namespace m=w3vr::camera_history_math;
using Record=w3vr::engine_camera::TemporalRecord;using C=w3vr::engine_camera::TemporalContract;
constexpr const C* c=&w3vr::engine_camera::remastered_1048522;
unsigned checks{};
void require(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
void put(void* dst,float value){std::memcpy(dst,&value,4);}
h::Prefix prefix(float marker=1){std::array<float,14> f{marker,2,3,0,0,0,0,90,0,0,2,1,1,101};h::Prefix p{};std::memcpy(p.data(),f.data(),p.size());
 const std::uint32_t word=0x7FA12345;std::memcpy(p.data()+12,&word,4);return p;}
// Independent analytic fixture: zero Euler, Y/Z axis exchange, translation,
// 90-degree lens and depth0 atnear1 / depth1 atfar101. No Game code executes.
Record fixture(float time=3){Record r{};auto p=prefix();r[0]=1;put(r.data()+4,time);put(r.data()+8,90);
 std::memcpy(r.data()+0x10,p.data(),16);std::memcpy(r.data()+0x20,p.data()+0x10,12);
 const m::Matrix v{1,0,0,0,0,0,1,0,0,1,0,0,-1,-3,-2,1};
 const m::Matrix pmat{.5f,0,0,0,0,1,0,0,0,0,1.01f,1,0,0,-1.01f,0};
 std::memcpy(r.data()+0x30,v.data(),64);std::memcpy(r.data()+0x70,pmat.data(),64);return r;}
std::array<double,4> transformed(const m::Matrix& matrix,std::array<float,4> point){std::array<double,4> out{};
 for(unsigned col=0;col<4;++col)for(unsigned row=0;row<4;++row)out[col]+=static_cast<double>(point[row])*matrix[row*4+col];return out;}
m::Matrix matrix(const Record& r,std::size_t offset){m::Matrix out{};std::memcpy(out.data(),r.data()+offset,64);return out;}
void math_checks(){Record out{};out.fill(0xA5);auto p=prefix();
 require(m::build(c,p,3,out) && m::matches_reference(c,p,3,fixture()),"analytic zero-Euler native-format fixture rejected");
 require(!std::memcmp(out.data()+0x10,p.data(),16),"opaque position word changed");
 const auto origin=transformed(matrix(out,0x30),{1,2,3,1});require(std::fabs(origin[0])+std::fabs(origin[1])+std::fabs(origin[2])<1e-6 && origin[3]==1,"camera origin does not map to zero");
 const auto near=transformed(matrix(out,0x70),{0,0,1,1}),far=transformed(matrix(out,0x70),{0,0,101,1});
 require(std::fabs(near[2]/near[3])<1e-6 && std::fabs(far[2]/far[3]-1)<1e-6,"record projection uses wrong depth convention");
 const std::array<m::Matrix,3> quarters{{
  {0,1,0,0,0,0,1,0,-1,0,0,0,0,0,0,1},
  {1,0,0,0,0,-1,0,0,0,0,1,0,0,0,0,1},
  {0,0,-1,0,1,0,0,0,0,1,0,0,0,0,0,1}}};
 for(unsigned axis=0;axis<3;++axis){auto q=prefix(0);put(q.data()+4,0);put(q.data()+8,0);put(q.data()+0x10+axis*4,90);
  require(m::build(c,q,3,out),"quarter turn could not be built");const auto got=matrix(out,0x30);
  for(unsigned k=0;k<16;++k)require(std::fabs(got[k]-quarters[axis][k])<2e-6,"quarter-turn axis/sign/basis incorrect");}
 for(const auto angles:std::array<std::array<float,3>,3>{{{13,29,-42},{0,90,72},{721,-89,540}}}){auto q=prefix();std::memcpy(q.data()+0x10,angles.data(),12);
  require(m::build(c,q,3,out),"mixed rotation rejected");auto v=matrix(out,0x30);
  for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){double dot{};for(unsigned k=0;k<3;++k)dot+=static_cast<double>(v[a*4+k])*v[b*4+k];require(std::fabs(dot-(a==b?1:0))<2e-6,"mixed view loses orthogonality");}
  auto point=transformed(v,{1,2,3,1});require(std::fabs(point[0])+std::fabs(point[1])+std::fabs(point[2])<2e-6,"mixed camera position transformed incorrectly");}
 for(unsigned offset:{4u,8u,0x10u,0x30u,0x50u,0x70u,0x98u}){auto bad=fixture();float value{};std::memcpy(&value,bad.data()+offset,4);put(bad.data()+offset,value+.1f);require(!m::matches_reference(c,p,3,bad),"incompatible observed reference accepted");}
 auto copied=*c;for(const C* bad:std::array<const C*,4>{nullptr,&copied,&w3vr::engine_camera::legacy_404,&w3vr::engine_camera::remastered_500c}){auto saved=out;require(!m::build(bad,p,3,out) && out==saved,"unknown/copied profile published matrix");}
 for(unsigned kind=0;kind<6;++kind){auto bad=p;if(kind==0)put(bad.data()+0x1C,180);if(kind==1)put(bad.data()+0x28,0);if(kind==2)put(bad.data()+0x2C,-1);if(kind==3)put(bad.data()+0x34,1);if(kind==4)put(bad.data(),INFINITY);if(kind==5)put(bad.data()+0x10,NAN);auto saved=out;require(!m::build(c,bad,3,out)&&out==saved,"invalid matrix input modified output");}
}
struct Setup {h::Plan plan;w3vr::temporal_source::Observation observed;w3vr::modern_camera_pose::PreparedPair pair;Record reference=fixture(),invalid{};};
Setup setup(){Setup s;s.plan.ticket=7;s.plan.inputs.identity={c,5,9,500,{0x8000,3,9}};s.plan.reset=h::ResetReason::none;s.plan.clock={3000000,1000000,3};s.observed.kind=w3vr::temporal_source::Kind::built;s.observed.clock=s.plan.clock;s.observed.previous=prefix();
 s.pair.contract=c;s.pair.display_time=500;const auto* layout=w3vr::engine_camera_layout::selected(c);s.invalid.fill(0x31);s.invalid[0]=0;
 for(unsigned e=0;e<2;++e){s.pair.descriptors[e].assign(0xF750,0xA5);std::uint32_t count=0;std::memcpy(s.pair.descriptors[e].data()+0xECA0,&count,4);
  for(unsigned camera=0;camera<2;++camera){s.plan.inputs.current[e][camera]=prefix(static_cast<float>(10+e*2+camera));s.plan.previous[e][camera]=prefix(static_cast<float>(1+e*2+camera));s.plan.inputs.center[e][camera]={.1f*e,.2f*camera};
   auto* dst=s.pair.descriptors[e].data()+layout->descriptor_cameras[camera];std::memcpy(dst,s.plan.inputs.current[e][camera].data(),h::Prefix{}.size());std::memcpy(dst+0x520,s.plan.inputs.center[e][camera].data(),8);}}
 s.plan.inputs.raw_primary=prefix();return s;}
bool equal(const w3vr::modern_camera_pose::PreparedPair& a,const w3vr::modern_camera_pose::PreparedPair& b){return a.contract==b.contract&&a.display_time==b.display_time&&a.descriptors==b.descriptors;}
bool field(std::size_t off){return off==0 || (off>=4&&off<12)||(off>=16&&off<44)||(off>=48&&off<176);}
void materialization_checks(){auto s=setup();const auto before=s.pair;require(h::materialize(s.plan,s.observed,s.reference,s.invalid,s.pair),"four owned histories not attached");
 const auto* layout=w3vr::engine_camera_layout::selected(c);
 for(unsigned e=0;e<2;++e)for(std::size_t byte=0;byte<s.pair.descriptors[e].size();++byte){bool allowed=false;for(unsigned k=0;k<2;++k){auto start=layout->descriptor_cameras[k]+c->previous_record_offset;if(byte>=start&&byte<start+w3vr::engine_camera::record_bytes&&field(byte-start))allowed=true;}
  if(!allowed)require(s.pair.descriptors[e][byte]==before.descriptors[e][byte],"pose/lens/ref/padding/unrelated descriptor byte changed");}
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k){const auto* dst=s.pair.descriptors[e].data()+layout->descriptor_cameras[k]+c->previous_record_offset;float pos{},time{};std::array<float,2> center{};std::memcpy(&pos,dst+0x10,4);std::memcpy(&time,dst+4,4);std::memcpy(center.data(),dst+0x90,8);
  require(pos==static_cast<float>(1+e*2+k)&&time==3&&center==s.plan.inputs.center[e][k],"HMD eye/internal camera/time/center exchanged");}
 for(unsigned kind=0;kind<8;++kind){auto bad=setup();switch(kind){case 0:put(bad.reference.data()+0x30,5);break;case 1:bad.plan.clock.time=4;break;case 2:bad.pair.display_time++;break;case 3:bad.plan.inputs.current[1][1][0]^=1;break;case 4:bad.invalid[0]=1;break;case 5:bad.plan.reset=static_cast<h::ResetReason>(99);break;case 6:bad.pair.descriptors[1].pop_back();break;case 7:bad.observed.clock.scale*=2;break;}
  const auto unchanged=bad.pair;require(!h::materialize(bad.plan,bad.observed,bad.reference,bad.invalid,bad.pair)&&equal(bad.pair,unchanged),"invalid source/plan published partial descriptor");}
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k){auto bad=setup();put(bad.plan.previous[e][k].data()+0x34,0);const auto unchanged=bad.pair;require(!h::materialize(bad.plan,bad.observed,bad.reference,bad.invalid,bad.pair)&&equal(bad.pair,unchanged),"one failed reconstruction modified the other eye");}
 auto reset=setup();reset.plan.reset=h::ResetReason::native_reset;reset.observed.kind=w3vr::temporal_source::Kind::reset;reset.observed.reset_record=reset.invalid;reset.reference.fill(0xFF);
 require(h::materialize(reset.plan,reset.observed,reset.reference,reset.invalid,reset.pair),"canonical native reset attempted valid reconstruction");
 for(unsigned e=0;e<2;++e)for(unsigned k=0;k<2;++k){auto* p=reset.pair.descriptors[e].data()+layout->descriptor_cameras[k]+c->previous_record_offset;for(std::size_t off=0;off<reset.invalid.size();++off)require(p[off]==(field(off)?reset.invalid[off]:0xA5),"reset record/padding copy incorrect");}
}
void captured_reference_checks(){namespace ts=w3vr::temporal_source;
 ts::Candidate candidate{};candidate.contract=c;candidate.descriptor=0x100000;candidate.owner=0x200000;candidate.director=0x300000;
 candidate.history=candidate.director+0x6D0;candidate.position=candidate.director+0xE0;candidate.rotation=candidate.position+0x10;candidate.thread=1;
 candidate.clock={3000000,1000000,3};candidate.time=3;candidate.fov=90;candidate.aspect=2;candidate.near_plane=1;candidate.far_plane=101;candidate.scale=1;candidate.prefix=prefix();
 ts::Captured captured;const auto reference=fixture();require(captured.capture(candidate,reference,candidate.history,true),"OWN reference receipt fixture rejected");
 Record out{};require(captured.built_record(out)&&out==reference,"copied observed reference missing");
 captured.revoke();out.fill(0xA5);const auto saved=out;require(!captured.built_record(out)&&out==saved,"revoked receipt exposed reference bytes");
 ts::Captured reset;ts::ResetCandidate rc{c,0x140000000,0x100000,0x200000,0x300000,0x3006D0,0x140000000+ts::reset_source,1,{3000000,1000000,3}};
 Record invalid{};require(reset.capture_reset(rc,invalid,invalid,rc.history,true)&&!reset.built_record(out)&&out==saved,"reset exposed built reference");
}
int main(){captured_reference_checks();math_checks();materialization_checks();std::printf("PASS owned history materialization: %u checks; analytic matrices/depth/axes, observed-reference rejection, four atomic private records and canonical reset.\n",checks);std::puts("GAME=false HOST_CALLBACKS=false EXTRA_NATIVE_BUILDER=false SDK_INIT=false GPU=false OPENXR_RUNTIME=false QUEST=false VR_FUNCTIONAL=false");}
