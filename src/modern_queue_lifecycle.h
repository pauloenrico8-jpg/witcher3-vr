#pragma once
#include "modern_command_queue.h"
#include <array>
#include <algorithm>
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
inline constexpr std::array<std::uint8_t,13> stop_signature{
 0x48,0x83,0xEC,0x28,0x48,0x83,0xC1,0x40,0xBA,0x10,0x00,0x00,0x00};
// Independently checked complete-instruction prefixes for the current build.
inline constexpr std::array<std::uint8_t,16> current_construct_signature{
 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
inline constexpr auto current_destroy_signature=current_construct_signature;
inline constexpr std::array<std::uint8_t,13> current_stop_signature{
 0x48,0x83,0xEC,0x28,0x48,0x83,0xC1,0x40,0xBA,0x10,0x00,0x00,0x00};
struct HookTarget {std::uintptr_t rva;std::span<const std::uint8_t> prefix;};
struct Profile {
 const modern_command_queue::Profile* queue;
 std::uintptr_t normal_return,alternate_return;
 std::array<HookTarget,3> hooks;
};
inline constexpr Profile remastered_500c{&modern_command_queue::remastered_500c,
 normal_construct_return,alternate_construct_return,
 {{{construct_rva,construct_signature},{stop_rva,stop_signature},{destroy_rva,destroy_signature}}}};
inline constexpr Profile remastered_1048522{&modern_command_queue::remastered_1048522,
 0x01BDF877,0x01BE0A33,
 {{{0x01C6A470,current_construct_signature},{0x01C6AA20,current_stop_signature},{0x0035AEF0,current_destroy_signature}}}};
inline bool known(const Profile* profile){return profile==&remastered_500c || profile==&remastered_1048522;}
inline const Profile* selected(const engine_camera::TemporalContract* contract){
 if(contract==&engine_camera::remastered_500c)return &remastered_500c;
 if(contract==&engine_camera::remastered_1048522)return &remastered_1048522;
 return nullptr;
}
inline bool ready_for(const engine_camera::TemporalContract* contract,const Profile* installed,bool ready){
 return ready && known(installed) && selected(contract)==installed &&
  modern_command_queue::selected(contract)==installed->queue;
}
inline bool known_construction(const Profile* profile,std::uintptr_t caller,std::uint8_t mode){
 return known(profile) && ((caller==profile->normal_return && mode==0) ||
  (caller==profile->alternate_return && mode==1));
}
inline bool prefix_matches(std::span<const std::uint8_t> expected,std::span<const std::uint8_t> actual){
 if(expected.empty() || actual.size()<expected.size())return false;
 return std::equal(expected.begin(),expected.end(),actual.begin());
}
// The host checks all Native prefixes before this helper and publishes readiness
// only after success. Failure retains created trampolines; no removal or retry.
// Disabling entries is NOT a barrier against already-entered or suspended calls.
template<class Create,class Enable,class Disable,class Current>
bool install_observers(const engine_camera::TemporalContract* contract,const Profile* profile,
 Create create,Enable enable,Disable disable,Current current){
 if(!known(profile) || selected(contract)!=profile)return false;
 std::size_t created{},enabled{};
 for(std::size_t i=0;i<profile->hooks.size();++i){if(!current() || !create(i))break;++created;}
 if(created==profile->hooks.size())for(std::size_t i=0;i<profile->hooks.size();++i){if(!current() || !enable(i))break;++enabled;}
 if(created==profile->hooks.size() && enabled==profile->hooks.size() && current())return true;
 for(std::size_t i=0;i<enabled;++i)disable(i);
 return false;
}
struct Construction {
 std::uintptr_t queue{};std::uint64_t revision{};const Profile* profile{};
 bool operator==(const Construction&)const=default;
};
struct Snapshot {
 modern_command_queue::Identity identity{};std::uint64_t revision{};
 bool operator==(const Snapshot&) const=default;
};
inline bool read_constructed_identity(const Profile* profile,std::uintptr_t module,std::uintptr_t renderer,
 std::span<const std::uint8_t> renderer_prefix,std::uintptr_t queue,
 std::span<const std::uint8_t> queue_prefix,modern_command_queue::Identity& output){
 namespace q=modern_command_queue;
 if(!known(profile))return false;
 const auto* queue_profile=profile->queue;
 std::uintptr_t renderer_table{},queue_table{};
 if(!q::known_profile(queue_profile) || renderer_prefix.size()<queue_profile->renderer_prefix ||
  queue_prefix.size()<constructed_prefix_bytes ||
  !q::add(module,queue_profile->renderer_vtable,renderer_table) ||
  !q::add(module,queue_profile->queue_vtable,queue_table) ||
  q::field<std::uintptr_t>(renderer_prefix,0)!=renderer_table ||
  q::field<std::uintptr_t>(renderer_prefix,queue_profile->renderer_queue)!=0 ||
  q::field<std::uintptr_t>(queue_prefix,0)!=queue_table ||
  q::field<std::uintptr_t>(queue_prefix,owner_offset)!=renderer)return false;
 q::Identity candidate{module,renderer,queue,q::field<std::uintptr_t>(queue_prefix,0x50),
  q::field<std::int32_t>(queue_prefix,0x40),q::field<std::int32_t>(queue_prefix,0x5C),queue_profile};
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
 Construction begin(std::uintptr_t queue,const Profile* profile,bool observers_ready){
  if(!queue)return {};
  std::lock_guard guard(mutex_);
  Entry* available{};
  for(auto& entry:entries_){
   if(entry.construction.queue==queue){entry={};available=&entry;}
   else if(!entry.construction.queue && !available)available=&entry;
  }
  // Always invalidate reuse, even during partial observer activation. Never
  // let a constructor that entered too early acquire a ticket on its return.
  if(!known(profile) || !observers_ready)return {};
  if(!next_ || next_==std::numeric_limits<std::uint64_t>::max()){
   entries_.fill({});return {};
  }
  if(!available)return {};
  const Construction construction{queue,next_++,profile};
  *available={construction,{},false};return construction;
 }
 bool finish(Construction construction,const modern_command_queue::Identity& identity,bool observers_ready){
  if(!observers_ready){cancel(construction);return false;}
  if(!construction.queue || !construction.revision || !known(construction.profile) ||
   construction.profile->queue!=identity.profile || construction.queue!=identity.queue ||
   !modern_command_queue::valid(identity))return false;
  std::lock_guard guard(mutex_);
  for(auto& entry:entries_)if(entry.construction==construction && !entry.complete){
   entry.identity=identity;entry.complete=true;return true;
  }
  return false;
 }
 void cancel(Construction construction){
  if(!construction.queue || !construction.revision)return;
  std::lock_guard guard(mutex_);
  for(auto& entry:entries_)if(entry.construction==construction)entry={};
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
