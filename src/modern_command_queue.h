#pragma once
#include "engine_frame_submission.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace w3vr::modern_command_queue {
// Borrowed native queue identity. Matching snapshots do not retain this
// allocation, serialize consumers or prove cache/GPU completion.
inline constexpr std::uintptr_t queue_vtable_rva=0x036E2160;
inline constexpr std::uintptr_t scene_command_vtable_rva=0x0394FFE8;
inline constexpr std::uintptr_t payload_allocator_rva=0x023220E0;
inline constexpr std::size_t renderer_queue_offset=0x110,allocator_offset=0x40;
inline constexpr std::size_t renderer_prefix_bytes=0x118,queue_prefix_bytes=0x64;
inline constexpr std::uint32_t payload_bytes=24,aligned_payload_bytes=32,record_bytes=48;
inline constexpr std::uint32_t unpublished_marker=0x4A545330;
inline constexpr std::int32_t ring_budget_difference=0x13880;
inline constexpr std::array<std::uint8_t,16> allocator_signature{
 0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
struct Identity {
 std::uintptr_t module{},renderer{},queue{},buffer{};
 std::int32_t budget_limit{},ring_bytes{};
 bool operator==(const Identity&) const=default;
};
inline bool add(std::uintptr_t base,std::size_t offset,std::uintptr_t& result){
 if(!base || base>std::numeric_limits<std::uintptr_t>::max()-offset)return false;
 result=base+offset;return true;
}
template<class T> inline T field(std::span<const std::uint8_t> bytes,std::size_t offset){
 T value{};std::memcpy(&value,bytes.data()+offset,sizeof(T));return value;
}
inline bool valid(const Identity& identity){
 std::uintptr_t end{},allocator{},table{};
 return identity.module && identity.renderer && identity.queue && identity.buffer &&
  !(identity.buffer&15) && identity.budget_limit>std::int32_t(2*record_bytes) &&
  identity.ring_bytes>identity.budget_limit &&
  std::int64_t(identity.ring_bytes)-identity.budget_limit==ring_budget_difference &&
  add(identity.queue,allocator_offset,allocator) &&
  add(identity.buffer,std::size_t(identity.ring_bytes)+0x3E80,end) &&
  add(identity.module,scene_command_vtable_rva,table);
}
inline bool read_identity(std::uintptr_t module,std::uintptr_t renderer,
 std::span<const std::uint8_t> renderer_prefix,std::uintptr_t queue,
 std::span<const std::uint8_t> queue_prefix,Identity& result){
 std::uintptr_t renderer_table{},queue_table{};
 if(renderer_prefix.size()<renderer_prefix_bytes || queue_prefix.size()<queue_prefix_bytes ||
  !add(module,frame_submission::renderer_vtable_rva,renderer_table) ||
  !add(module,queue_vtable_rva,queue_table) ||
  field<std::uintptr_t>(renderer_prefix,0)!=renderer_table ||
  field<std::uintptr_t>(renderer_prefix,renderer_queue_offset)!=queue ||
  field<std::uintptr_t>(queue_prefix,0)!=queue_table)return false;
 Identity candidate{module,renderer,queue,field<std::uintptr_t>(queue_prefix,0x50),
  field<std::int32_t>(queue_prefix,0x40),field<std::int32_t>(queue_prefix,0x5C)};
 if(!valid(candidate))return false;
 result=candidate;return true;
}
inline bool primary_command_matches(const Identity& identity,std::uintptr_t command,
 std::uintptr_t frame,std::span<const std::uint8_t> header,
 std::span<const std::uint8_t> payload){
 if(!valid(identity) || command<16 || !frame || header.size()<16 || payload.size()<payload_bytes)return false;
 const auto start=command-16;
 if(start<identity.buffer || ((start-identity.buffer)&15) ||
  start-identity.buffer>=std::size_t(identity.ring_bytes)-aligned_payload_bytes)return false;
 // The original allocator tests start+alignedPayload < ringBytes; its full
 // allocation has extra tail space. Do not invent a different wrap rule.
 return field<std::uint32_t>(header,0)==unpublished_marker &&
  field<std::uint32_t>(header,4)==aligned_payload_bytes &&
  field<std::uintptr_t>(payload,0)==identity.module+scene_command_vtable_rva &&
  field<std::uintptr_t>(payload,8)==frame && field<std::uintptr_t>(payload,16)==0;
}
#if defined(_MSC_VER)
using AllocatePayload=void*(__fastcall*)(void*,std::uint32_t);
#else
using AllocatePayload=void*(*)(void*,std::uint32_t);
#endif
inline void* allocate_bound(AllocatePayload original,const Identity& captured,const Identity& current){
 if(!original || !valid(captured) || !valid(current) || captured!=current)return nullptr;
 // Use the same actual native receiver, not another global queue lookup.
 // Original allocator owns reservations, headers, wrap and capacity waiting.
 return original(reinterpret_cast<void*>(captured.queue+allocator_offset),payload_bytes);
}
} // namespace w3vr::modern_command_queue
