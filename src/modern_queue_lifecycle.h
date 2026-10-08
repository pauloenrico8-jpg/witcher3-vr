#pragma once
#include "modern_command_queue.h"
#include <array>
#include <mutex>

namespace w3vr::modern_queue_lifecycle {
// Observation only: a snapshot is not a retained native allocation or a lease.
// Native stop may race after a successful check. External lifetime admission,
// all destruction routes, consumer exclusivity and GPU completion remain needed.
inline constexpr std::uintptr_t construct_rva=0x01C62F80,stop_rva=0x01C63530,destroy_rva=0x0035C450;
inline constexpr std::uintptr_t normal_construct_return=0x01BD8FA2,alternate_construct_return=0x01BDA153;
inline constexpr std::size_t owner_offset=0x68,constructed_prefix_bytes=0x70;
inline constexpr std::array<std::uint8_t,16> construct_signature{
 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
inline constexpr auto destroy_signature=construct_signature;
inline constexpr std::array<std::uint8_t,16> stop_signature{
 0x48,0x83,0xEC,0x28,0x48,0x83,0xC1,0x40,0xBA,0x10,0x00,0x00,0x00,0xE8,0x9E,0xEB};
inline bool known_construction(std::uintptr_t caller,std::uint8_t mode){
 return (caller==normal_construct_return && mode==0) ||
  (caller==alternate_construct_return && mode==1);
}
struct Construction {std::uintptr_t queue{};std::uint64_t revision{};};
struct Snapshot {
 modern_command_queue::Identity identity{};std::uint64_t revision{};
 bool operator==(const Snapshot&) const=default;
};
inline bool read_constructed_identity(std::uintptr_t module,std::uintptr_t renderer,
 std::span<const std::uint8_t> renderer_prefix,std::uintptr_t queue,
 std::span<const std::uint8_t> queue_prefix,modern_command_queue::Identity& output){
 namespace q=modern_command_queue;
 std::uintptr_t renderer_table{},queue_table{};
 if(renderer_prefix.size()<q::renderer_prefix_bytes || queue_prefix.size()<constructed_prefix_bytes ||
  !q::add(module,frame_submission::renderer_vtable_rva,renderer_table) ||
  !q::add(module,q::queue_vtable_rva,queue_table) ||
  q::field<std::uintptr_t>(renderer_prefix,0)!=renderer_table ||
  q::field<std::uintptr_t>(renderer_prefix,q::renderer_queue_offset)!=0 ||
  q::field<std::uintptr_t>(queue_prefix,0)!=queue_table ||
  q::field<std::uintptr_t>(queue_prefix,owner_offset)!=renderer)return false;
 q::Identity candidate{module,renderer,queue,q::field<std::uintptr_t>(queue_prefix,0x50),
  q::field<std::int32_t>(queue_prefix,0x40),q::field<std::int32_t>(queue_prefix,0x5C)};
 if(!q::valid(candidate))return false;
 output=candidate;return true;
}
class Observations {
 // Fixed storage avoids allocating memory inside native construction hooks.
 struct Entry {Construction construction{};modern_command_queue::Identity identity{};bool complete{};};
 std::array<Entry,64> entries_{};
 mutable std::mutex mutex_;
 std::uint64_t next_;
public:
 explicit Observations(std::uint64_t first_revision=1):next_(first_revision){}
 Construction begin(std::uintptr_t queue,bool observers_ready){
  if(!queue)return {};
  std::lock_guard guard(mutex_);
  Entry* available{};
  for(auto& entry:entries_){
   if(entry.construction.queue==queue){entry={};available=&entry;}
   else if(!entry.construction.queue && !available)available=&entry;
  }
  // Always invalidate reuse, even during partial observer activation. Never
  // let a constructor that entered too early acquire a ticket on its return.
  if(!observers_ready)return {};
  if(!next_ || next_==std::numeric_limits<std::uint64_t>::max()){
   entries_.fill({});return {};
  }
  if(!available)return {};
  const Construction construction{queue,next_++};
  *available={construction,{},false};return construction;
 }
 bool finish(Construction construction,const modern_command_queue::Identity& identity,bool observers_ready){
  if(!observers_ready){cancel(construction);return false;}
  if(!construction.queue || !construction.revision || construction.queue!=identity.queue ||
   !modern_command_queue::valid(identity))return false;
  std::lock_guard guard(mutex_);
  for(auto& entry:entries_)if(entry.construction.queue==construction.queue &&
    entry.construction.revision==construction.revision && !entry.complete){
   entry.identity=identity;entry.complete=true;return true;
  }
  return false;
 }
 void cancel(Construction construction){
  if(!construction.queue || !construction.revision)return;
  std::lock_guard guard(mutex_);
  for(auto& entry:entries_)if(entry.construction.queue==construction.queue &&
    entry.construction.revision==construction.revision)entry={};
 }
 void retire(std::uintptr_t queue){
  std::lock_guard guard(mutex_);
  for(auto& entry:entries_)if(entry.construction.queue==queue)entry={};
 }
 bool snapshot(const modern_command_queue::Identity& identity,Snapshot& output) const{
  if(!modern_command_queue::valid(identity))return false;
  std::lock_guard guard(mutex_);
  for(const auto& entry:entries_)if(entry.complete && entry.identity==identity){
   output={identity,entry.construction.revision};return true;
  }
  return false;
 }
 bool current(const Snapshot& expected)const{
  Snapshot observed{};
  return expected.revision && snapshot(expected.identity,observed) && observed==expected;
 }
};
} // namespace w3vr::modern_queue_lifecycle
