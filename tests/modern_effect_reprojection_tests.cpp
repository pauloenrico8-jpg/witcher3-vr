#include "modern_effect_reprojection.h"
#include <iostream>
#include <vector>
#include <cstdlib>
using namespace w3vr::effect_reprojection;
unsigned checks{};
void check(bool x){++checks;if(!x){std::cerr<<"FAIL check "<<checks<<'\n';std::exit(1);}}
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
std::array<double,4> point(std::array<double,4> v,const Matrix& m){std::array<double,4> o{};for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)o[j]+=v[i]*m[i*4+j];return o;}
void close(double a,double b){check(std::fabs(a-b)<3e-5*(1+std::max(std::fabs(a),std::fabs(b))));}
Snapshot simple(){Snapshot s;s.valid=1;s.current_projection=s.previous_projection=s.current_view=identity();s.previous_view={2,0,0,0,0,3,0,0,0,0,4,0,0,0,0,1};s.observed={2,0,0,0,0,3,0,0,0,0,4,0,0,0,-3,1};s.center=s.previous_center={.25f,-.4f};return s;}
Snapshot perspective(float cx,float cy){
    Snapshot s;s.valid=1;s.center=s.previous_center={cx,cy};s.current_view=identity();s.previous_view=identity();
    s.previous_view[12]=.3f;s.previous_view[13]=-.2f;s.previous_view[14]=.4f;
    s.current_projection=s.previous_projection={.5f,0,0,0,0,1,0,0,0,0,1.01f,1,0,0,-1.01f,0};
    // Independent analytic inverse of the listed perspective, then translated
    // view and projection. No call to the implementation's inverse/multiply.
    for(unsigned i=0;i<4;++i){std::array<double,4> c{};c[i]=1;
        const double f=s.current_projection[10],a=s.current_projection[0],b=s.current_projection[5];
        const double z=c[3],w=(c[2]-(1-f)*z)/f;
        const double x=c[0]/a+s.previous_view[12]*w,y=c[1]/b+s.previous_view[13]*w,old_z=z+s.previous_view[14]*w;
        const std::array<double,4> old{a*x,b*y,(1-f)*old_z+f*w,old_z};
        for(unsigned j=0;j<4;++j)s.observed[i*4+j]=static_cast<float>(old[j]);
    }return s;
}
void geometry(){
 auto s=simple();Matrix out{};check(consistent(s));check(corrected(s,out));close(out[12],-.25);close(out[13],.8);close(out[14],-3);
 for(auto center:std::array<std::array<float,2>,4>{{{.3f,-.4f},{-.2f,.1f},{0,0},{.7f,.5f}}}){
  s=perspective(center[0],center[1]);check(consistent(s));check(corrected(s,out));
  bool naive_wrong=false;
  for(double x:{-.4,.6})for(double y:{-.5,.7})for(double z:{1.5,4.,15.}){
   std::array<double,4> world{x,y,z,1};auto c=point(world,s.current_projection);c[2]=c[3]-c[2];c[0]+=center[0]*c[3];c[1]+=center[1]*c[3];
   auto old=point(point(world,s.previous_view),s.previous_projection);old[2]=old[3]-old[2];old[0]+=center[0]*old[3];old[1]+=center[1]*old[3];
   const auto actual=point(c,out),naive=point(c,s.observed);for(unsigned j=0;j<4;++j)close(actual[j],old[j]);
   naive_wrong|=std::fabs(naive[0]/naive[3]-old[0]/old[3])>1e-5 || std::fabs(naive[1]/naive[3]-old[1]/old[3])>1e-5;
  }check(naive_wrong==(center[0]!=0 || center[1]!=0));
 }
}
void packets(){const auto* c=&w3vr::engine_camera::remastered_1048522;auto s=perspective(.3f,-.4f);Matrix matrix{};check(corrected(s,matrix));
 for(const auto& producer:producers){std::vector<std::uint8_t> source(producer.bytes,0x4b),out(producer.bytes+8,0xa7);std::memcpy(source.data()+producer.matrix,s.observed.data(),64);const auto original=source;
  check(prepare(c,producer.caller,s,source,out));check(source==original);
  for(unsigned i=0;i<out.size();++i)check(out[i]==(i>=producer.bytes ? 0xa7 : i>=producer.matrix && i<producer.matrix+64 ? reinterpret_cast<const std::uint8_t*>(matrix.data())[i-producer.matrix] : source[i]));
  const auto saved=out;check(!prepare(c,producer.caller+1,s,source,out));check(out==saved);
  check(!prepare(c,producer.caller,s,std::span<const std::uint8_t>(source).first(source.size()-1),out));check(out==saved);
  check(!prepare(c,producer.caller,s,source,std::span<std::uint8_t>(out).first(source.size()-1)));check(out==saved);
  source[producer.matrix]^=1;check(!prepare(c,producer.caller,s,source,out));check(out==saved);source=original;
  auto copied=*c;for(const auto* contract:std::array<const w3vr::engine_camera::TemporalContract*,4>{nullptr,&copied,&w3vr::engine_camera::legacy_404,&w3vr::engine_camera::remastered_500c}){check(!prepare(contract,producer.caller,s,source,out));check(out==saved);}
  auto alias=original;check(prepare(c,producer.caller,s,alias,alias));check(std::memcmp(alias.data()+producer.matrix,matrix.data(),64)==0);
  std::vector<std::uint8_t> overlap(producer.bytes+1);std::memcpy(overlap.data(),original.data(),producer.bytes);check(prepare(c,producer.caller,s,std::span<const std::uint8_t>(overlap).first(producer.bytes),std::span<std::uint8_t>(overlap).subspan(1)));check(std::memcmp(overlap.data()+1+producer.matrix,matrix.data(),64)==0);
 }
}
void rejection(){auto s=simple();Matrix out{};out.fill(19);const auto saved=out;
 for(unsigned i=0;i<16;++i){auto wrong=s;wrong.observed[i]+=1;check(!corrected(wrong,out));check(out==saved);}
 for(unsigned i=0;i<16;++i){auto wrong=s;wrong.current_view[i]=NAN;check(!corrected(wrong,out));check(out==saved);}
 for(unsigned i=0;i<2;++i){auto wrong=s;wrong.center[i]=INFINITY;check(!corrected(wrong,out));check(out==saved);wrong=s;wrong.previous_center[i]+=.1f;check(!corrected(wrong,out));check(out==saved);}
 s.valid=0;check(!corrected(s,out));check(out==saved);s=simple();s.current_projection.fill(0);check(!corrected(s,out));check(out==saved);
 DoubleMatrix bad{};DoubleMatrix output{};output.fill(23);check(!inverse(bad,output));for(double x:output)check(x==23);
}
void reading(){auto s=perspective(.3f,-.4f);std::array<std::uint8_t,0x5E0> camera{};camera[0x530]=1;
 for(auto entry:std::array<std::pair<std::size_t,const Matrix*>,5>{{{0x400,&s.observed},{0x440,&s.current_projection},{0x480,&s.previous_projection},{0x40,&s.current_view},{0x560,&s.previous_view}}})std::memcpy(camera.data()+entry.first,entry.second->data(),64);
 std::memcpy(camera.data()+0x520,s.center.data(),8);std::memcpy(camera.data()+0x5C0,s.previous_center.data(),8);Snapshot out{};check(read(camera,out));check(out==s);auto saved=out;check(!read(std::span<const std::uint8_t>(camera).first(camera.size()-1),out));check(out==saved);
}
void provenance(){
 for(const auto& p:producers){const auto index=p.source==SourceRegister::rdi ? 0u : p.source==SourceRegister::rsi ? 1u : 2u;
  std::array<std::uintptr_t,3> regs{0x800,0x900,0xA00};regs[index]=0x1234;check(source_matches(&p,0x1234,regs[0],regs[1],regs[2]));
  regs[index]=0x5678;check(!source_matches(&p,0x1234,regs[0],regs[1],regs[2]));auto copy=p;check(!source_matches(&copy,0x5678,regs[0],regs[1],regs[2]));check(!source_matches(&p,0,regs[0],regs[1],regs[2]));
 }check(!source_matches(nullptr,0x1234,0x1234,0x1234,0x1234));
}
int main(){provenance();geometry();packets();rejection();reading();std::cout<<"PASS effect reprojection: "<<checks<<" checks; analytic clip points, three packet layouts, preservation, aliasing and refusals.\nGAME=false HOST_CALLBACKS=false HOST_MINHOOK=false SDK=false GPU=false QUEST=false VR_FUNCTIONAL=false\n";}
