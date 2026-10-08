#include "modern_command_queue.h"
#include <cstdio>
namespace q=w3vr::modern_command_queue;
namespace {
int checks{},failures{},calls{};void* receiver{};std::uint32_t size{};void* answer{};
void require(bool ok,const char* why){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
template<class T,std::size_t N>void put(std::array<std::uint8_t,N>& b,std::size_t offset,T value){std::memcpy(b.data()+offset,&value,sizeof(value));}
struct Fixture {
 std::array<std::uint8_t,q::renderer_prefix_bytes> renderer{};
 std::array<std::uint8_t,0xA0> queue{};
 std::array<std::uint8_t,16> header{};
 std::array<std::uint8_t,q::payload_bytes> command{};
 q::Identity identity{0x140000000,0x300000,0x400000,0x200000,120000,200000};
 std::uintptr_t primary=0x500000,pointer=0x200010;
 Fixture(){
  put(renderer,0,identity.module+w3vr::frame_submission::renderer_vtable_rva);
  put(renderer,q::renderer_queue_offset,identity.queue);
  put(queue,0,identity.module+q::queue_vtable_rva);put(queue,0x40,identity.budget_limit);
  put(queue,0x50,identity.buffer);put(queue,0x5C,identity.ring_bytes);
  put(header,0,q::unpublished_marker);put(header,4,q::aligned_payload_bytes);
  put(command,0,identity.module+q::scene_command_vtable_rva);put(command,8,primary);put(command,16,std::uintptr_t{});
 }
 bool read(q::Identity& out){return q::read_identity(identity.module,identity.renderer,renderer,identity.queue,queue,out);}
 bool match(){return q::primary_command_matches(identity,pointer,primary,header,command);}
};
void* allocate_spy(void* actual,std::uint32_t bytes){++calls;receiver=actual;size=bytes;return answer;}
void valid_binding_and_mutable_cursor(){Fixture f;q::Identity parsed{};
 require(f.read(parsed)&&parsed==f.identity,"native queue identity not parsed");
 require(f.match(),"original unpublished primary record not matched");
 put(f.queue,0x44,std::int32_t(48));put(f.queue,0x48,std::int32_t(1));
 put(f.queue,0x58,std::uint32_t(48));put(f.queue,0x60,f.identity.buffer+48);
 put(f.queue,0x98,std::uint32_t(777));
 q::Identity changed{};require(f.read(changed)&&changed==parsed,"live cursor/budget/count wrongly became queue identity");
 calls=0;answer=reinterpret_cast<void*>(0xFEDC);
 require(q::allocate_bound(allocate_spy,parsed,changed)==answer&&calls==1&&
  receiver==reinterpret_cast<void*>(f.identity.queue+0x40)&&size==24,"native receiver/payload/return changed");
 // Bound native receiver stays explicit even when a global lookup would have
 // selected another queue. A changed identity is rejected before allocation.
 changed.queue+=0x1000;calls=0;
 require(q::allocate_bound(allocate_spy,parsed,changed)==nullptr&&calls==0,"different actual queue was allocated");
 for(auto offset:{0u,16u,320u,199952u}){f.pointer=f.identity.buffer+offset+16;
  require(f.match(),"valid native reservation position lost");}
 f.pointer=f.identity.buffer+std::uintptr_t(f.identity.ring_bytes-q::aligned_payload_bytes)+16;
 require(!f.match(),"native strict wrap boundary was widened");
}
void reject_corrupt_identity(){
 for(int reason=0;reason<16;++reason){Fixture f;q::Identity out{};out.buffer=0xBAD;
  std::span<const std::uint8_t> rp=f.renderer,qp=f.queue;
  switch(reason){case 0:f.identity.module=0;break;case 1:f.identity.renderer=0;break;case 2:f.identity.queue=0;break;
  case 3:put(f.renderer,0,f.identity.module+w3vr::frame_submission::renderer_vtable_rva+8);break;
  case 4:put(f.renderer,q::renderer_queue_offset,f.identity.queue+16);break;
  case 5:put(f.queue,0,f.identity.module+q::queue_vtable_rva+8);break;
  case 6:put(f.queue,0x50,std::uintptr_t{});break;case 7:put(f.queue,0x50,f.identity.buffer+1);break;
  case 8:put(f.queue,0x40,std::int32_t(96));put(f.queue,0x5C,std::int32_t(80096));break;
  case 9:put(f.queue,0x40,std::int32_t(-1));put(f.queue,0x5C,std::int32_t(79999));break;
  case 10:put(f.queue,0x5C,std::int32_t(-1));break;case 11:put(f.queue,0x5C,std::int32_t(200001));break;
  case 12:f.identity.module=UINTPTR_MAX-8;break;
  case 13:put(f.queue,0x50,std::uintptr_t(UINTPTR_MAX-15));break;
  case 14:rp=rp.first(q::renderer_prefix_bytes-1);break;case 15:qp=qp.first(q::queue_prefix_bytes-1);break;}
  require(!q::read_identity(f.identity.module,f.identity.renderer,rp,f.identity.queue,qp,out)&&out.buffer==0xBAD,
   "invalid queue admitted or rejected output mutated");
 }
 for(int reason=0;reason<8;++reason){Fixture f;auto current=f.identity;
  switch(reason){case 0:current.module+=16;break;case 1:current.renderer+=16;break;case 2:current.queue+=16;break;
  case 3:current.buffer+=16;break;case 4:current.budget_limit+=16;current.ring_bytes+=16;break;
  case 5:current.queue=UINTPTR_MAX-8;break;case 6:current.budget_limit=96;current.ring_bytes=80096;break;
  case 7:current.buffer=0;break;}
  calls=0;require(q::allocate_bound(allocate_spy,f.identity,current)==nullptr&&calls==0,"changed/invalid identity called native allocator");
 }
 Fixture f;require(q::allocate_bound(nullptr,f.identity,f.identity)==nullptr,"null allocator called");
}
void reject_foreign_or_published_primary(){
 for(int reason=0;reason<12;++reason){Fixture f;std::span<const std::uint8_t> header=f.header,payload=f.command;
  switch(reason){case 0:f.pointer=8;break;case 1:f.pointer=f.identity.buffer;break;case 2:f.pointer+=1;break;
  case 3:f.pointer=f.identity.buffer+f.identity.ring_bytes+16;break;
  case 4:put(f.header,0,std::uint32_t(0x45584543));break;case 5:put(f.header,4,std::uint32_t(64));break;
  case 6:put(f.command,0,f.identity.module+q::scene_command_vtable_rva+8);break;
  case 7:put(f.command,8,f.primary+8);break;case 8:put(f.command,16,std::uintptr_t(1));break;
  case 9:f.primary=0;break;case 10:header=header.first(15);break;case 11:payload=payload.first(23);break;}
  calls=0;
  const bool match=q::primary_command_matches(f.identity,f.pointer,f.primary,header,payload);
  if(match)q::allocate_bound(allocate_spy,f.identity,f.identity);
  require(!match&&calls==0,"foreign/published/malformed primary authorized duplicate allocation");
 }
}
}
int main(){valid_binding_and_mutable_cursor();reject_corrupt_identity();reject_foreign_or_published_primary();
 std::printf("Modern captured native queue: %d checks, %d failures (CPU only)\n",checks,failures);return failures?1:0;}
