#include "modern_eye_capture.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <stdexcept>
namespace cap=w3vr::modern_eye_capture;
namespace core=w3vr::render_core;
namespace cam=w3vr::engine_camera;
int checks{};
void require(bool yes,const char* text){++checks;if(!yes){std::cerr<<text<<'\n';std::exit(1);}}
cap::Identity live{&core::remastered_1048522,0x1000,{0x2000,7,99,0,true,true}};
cap::Request sentinel{0xEE,0xFF,88,777,1};
const cap::Transition render_end{0,4,0,UINT32_MAX};
bool request(const cap::Identity& identity,cap::Request& out,
 const cap::Transition& t=render_end,const cam::TemporalContract* contract=&cam::remastered_1048522,
 const core::Profile* installed=&core::remastered_1048522,bool ready=true,
 std::uint32_t generation=7,std::uintptr_t command=0x3000,bool endpoint=true,
 std::uintptr_t source=0x4000,bool current=true,bool single=true){
 return cap::prepare(identity,contract,installed,ready,generation,command,endpoint,source,current,single,t,out);
}
void rejects(const cap::Identity& identity,const char* why){auto out=sentinel;
 require(!request(identity,out),why);require(out.command==sentinel.command&&out.source==sentinel.source&&out.pair==sentinel.pair&&out.generation==sentinel.generation&&out.eye==sentinel.eye,"Rejected request changed output");}
cap::Identity thread_state{};
cap::Identity read(){return thread_state;} void apply(const cap::Identity& i){thread_state=i;}
int main(){
 auto out=sentinel;require(request(live,out),"Valid final backbuffer did not yield a copy request");
 require(out.command==0x3000&&out.source==0x4000&&out.eye==0&&out.pair==99&&out.generation==7,"Request lost exact eye/list/source");
 // Two eyes on the SAME recording list retain their distinct copy tickets.
 std::vector<cap::Request> copies{out};auto right=live;right.frame.eye=1;
 require(request(right,out),"Peer eye rejected");copies.push_back(out);
 require(copies[0].command==copies[1].command&&copies[0].eye==0&&copies[1].eye==1&&copies[0].pair==copies[1].pair,"Shared list overwrote its first eye");
 auto other=live;other.profile=&core::remastered_500c;rejects(other,"Another version supplied eye authority");
 auto copied=core::remastered_1048522;other=live;other.profile=&copied;rejects(other,"Copied profile was accepted");
 other=live;other.renderer=0;rejects(other,"Missing renderer accepted");other=live;other.frame.frame=0;rejects(other,"Missing frame accepted");
 other=live;other.frame.generation=6;rejects(other,"Old generation accepted");other=live;other.frame.pair=0;rejects(other,"Zero pair accepted");other.frame.pair=UINT64_MAX;rejects(other,"Reserved pair accepted");
 for(int eye:{-1,2}){other=live;other.frame.eye=eye;rejects(other,"Unknown eye accepted");}
 other=live;other.frame.view_valid=false;rejects(other,"Missing frozen view accepted");other=live;other.frame.normal_factory_lineage=false;rejects(other,"Private or incomplete frame accepted");
 for(auto t:{cap::Transition{1,4,0,0},cap::Transition{2,4,0,0},cap::Transition{0,0,0,0},cap::Transition{0,4,4,0},cap::Transition{0,4,0,1},cap::Transition{0,8,0,0}}){out=sentinel;require(!request(live,out,t),"Split, nonwrite or incomplete transition accepted");require(out.command==sentinel.command,"Rejected transition changed request");}
 require(request(live,out,{0,0x400,0,0}),"Copy-destination final buffer rejected");
 out=sentinel;require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,false),"Closed hooks accepted");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_500c),"Other installed bundle accepted");
 require(!request(live,out,render_end,&cam::legacy_404),"Legacy contract used modern path");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,true,8),"Generation rollover accepted");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,true,7,0),"Null list accepted");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,true,7,0x3000,false),"Unknown wrapper used as execution endpoint");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,true,7,0x3000,true,0),"Null source accepted");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,true,7,0x3000,true,0x4000,false),"Equal-sized intermediate or noncurrent backbuffer accepted");
 require(!request(live,out,render_end,&cam::remastered_1048522,&core::remastered_1048522,true,7,0x3000,true,0x4000,true,false),"Partial texture transition authorized full copy");
 // An unknown/private nested native call masks outer eye authority; unwind
 // restores the exact original identity even when its work throws.
 thread_state=live;
 try{core::CallState<cap::Identity> scope(read,apply,{});require(!request(thread_state,out),"Private nested call inherited outer eye");throw std::runtime_error("fixture");}catch(const std::runtime_error&){}
 require(thread_state.profile==live.profile&&thread_state.frame.eye==live.frame.eye&&request(thread_state,out),"Scope did not restore exact outer authority");
 auto old=live;old.profile=&core::remastered_500c;thread_state=old;
 {core::CallState<cap::Identity> scope(read,apply,live);require(request(thread_state,out),"Independent inner profile failed");}
 require(thread_state.profile==old.profile&&!request(thread_state,out),"Outer version was relabelled by inner call");
 std::cout<<checks<<" CPU checks: exact eye copy requests, reused lists, scoped authority and rejection. No game/GPU/Quest executed.\n";
}
