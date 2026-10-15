#pragma once
#include "engine_frame_preparation.h"
#include "engine_scene_publication.h"
namespace w3vr::publication_pair {
// These are CPU identities, not retained Native objects or GPU results.
struct Identity {
 const frame_preparation::Profile* profile{};
 std::uintptr_t command{},primary{},duplicate{};
 std::uint64_t pair{},generation{};
 bool operator==(const Identity&)const=default;
};
struct Ticket {Identity identity{};std::uint64_t serial{};};
class Pair {
 Identity identity_{};std::uint64_t serial_{};
 bool staged_{},extra_{},entered_{},complete_{},cancelled_{},cleanup_{};
public:
 bool stage(Identity i){
  if(staged_ || !frame_preparation::known(i.profile) || !i.command || !i.primary ||
    !i.duplicate || i.primary==i.duplicate || !i.pair || !i.generation)return false;
  identity_=i;staged_=true;return true;
 }
 bool extra_published(bool confirmed){
  if(!staged_ || extra_ || entered_ || complete_ || cancelled_)return false;
  if(!confirmed){cancelled_=true;return false;}extra_=true;return true;
 }
 bool before(const Identity& i,bool ready,Ticket& out){
  if(!staged_ || !extra_ || entered_ || complete_ || cancelled_ || !ready ||
    i!=identity_ || serial_==UINT64_MAX)return false;
  entered_=true;out={identity_,++serial_};return true;
 }
 bool after(Ticket t,bool ready,std::uint64_t raw_rax){
  if(!entered_ || complete_ || cancelled_ || !t.serial || t.serial!=serial_ ||
    t.identity!=identity_)return false;
  entered_=false;
  if(!ready || !scene_publication::published(raw_rax)){cancelled_=true;return false;}
  complete_=true;return true;
 }
 bool complete()const{return complete_;}
 bool needs_cancel()const{return staged_ && !complete_ && !cleanup_;}
 bool staged()const{return staged_;}
 const Identity& identity()const{return identity_;}
 void cancel(){cancelled_=cleanup_=true;entered_=false;}
};
}
