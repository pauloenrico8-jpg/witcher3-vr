#pragma once

#include "engine_camera_layout.h"
#include "engine_render_core.h"
#include <cmath>

namespace w3vr::world_culling_view {
// The native consumer supplies a borrowed frame/scene on its executing thread.
// Registry identity is not ownership, a worker lease or proof of GPU completion.
inline constexpr std::uintptr_t consumer_rva=0x01BF5420;
inline constexpr std::uintptr_t consumer_return=0x01BF2CA1;
inline constexpr std::uintptr_t camera_copy_return=0x01BF54B1;
inline constexpr std::size_t scene_manager_offset=0x80518;
inline constexpr std::size_t primary_offset=0x20,secondary_offset=0x600;
inline constexpr std::size_t camera_bytes=engine_camera_layout::remastered_500c.camera_bytes;
inline constexpr std::array<std::uint8_t,16> entry_signature{
    0x40,0x55,0x53,0x57,0x41,0x55,0x48,0x8D,0xAC,0x24,0xF8,0x67,0xFE,0xFF,0xB8,0x08};
inline bool add(std::uintptr_t base,std::size_t offset,std::uintptr_t& out) {
    if(!base || base>std::numeric_limits<std::uintptr_t>::max()-offset)return false;
    out=base+offset;return true;
}
struct Context {
    std::uintptr_t manager{},scene{},frame{};
    render_core::FrameLabel label{};
    bool normal_task{};
};
inline bool accepts(const engine_camera::TemporalContract* contract,const Context& context,
    std::uint32_t generation) {
    return contract==&engine_camera::remastered_500c && context.normal_task &&
        context.manager && context.scene &&
        render_core::accepts_frame_identity(context.frame,&context.label,generation);
}
inline bool bind(const engine_camera::TemporalContract* contract,std::uintptr_t caller,
    std::uintptr_t manager,std::uintptr_t scene,std::uintptr_t frame,
    const render_core::FrameLabel& label,std::uint32_t generation,
    std::uintptr_t observed_manager,Context& result) {
    Context candidate{manager,scene,frame,label,true};
    if(caller!=consumer_return || manager!=observed_manager || !accepts(contract,candidate,generation))return false;
    result=candidate;return true;
}
inline bool camera_valid(std::span<const std::uint8_t> bytes) {
    engine_camera::TemporalInputs inputs{};
    if(bytes.size()<camera_bytes || !engine_camera::read_temporal_inputs(bytes,0,inputs) ||
        inputs.fov>=180 || inputs.projection_scale<=0)return false;
    // The stable optical center will be read by the original native copy and
    // rebuild. Validate it without changing the camera or replaying HMD pose.
    float center[2]{};std::memcpy(center,bytes.data()+0x520,sizeof(center));
    return std::isfinite(center[0]) && std::isfinite(center[1]);
}
inline bool select_primary(const engine_camera::TemporalContract* contract,const Context& context,
    std::uint32_t generation,std::uintptr_t caller,std::uintptr_t destination,std::uintptr_t source,
    std::span<const std::uint8_t> primary,std::span<const std::uint8_t> secondary,std::uintptr_t& result) {
    std::uintptr_t p{},s{},pend{},send{},dend{};
    if(!accepts(contract,context,generation) || caller!=camera_copy_return ||
        !add(context.frame,primary_offset,p) || !add(context.frame,secondary_offset,s) ||
        !add(p,camera_bytes,pend) || !add(s,camera_bytes,send) ||
        source!=s || !add(destination,camera_bytes,dend) ||
        (destination<pend && p<dend) || (destination<send && s<dend) ||
        !camera_valid(primary) || !camera_valid(secondary))return false;
    result=p;return true;
    // Use the LIVE native primary address, never the copied byte snapshot as
    // a constructed camera. Original copy/rebuild/destruction stay native.
}
#if defined(_MSC_VER)
using NativeConsumer=void(__fastcall*)(void*,void*,void*);
using NativeCameraCopy=float*(__fastcall*)(float*,const float*);
#else
using NativeConsumer=void(*)(void*,void*,void*);
using NativeCameraCopy=float*(*)(float*,const float*);
#endif
inline float* invoke_copy(NativeCameraCopy original,float* destination,const float* source,
    std::uintptr_t selected_source) {
    return original(destination,selected_source?reinterpret_cast<const float*>(selected_source):source);
    // Preserve the native destination and return value; no manual camera clone.
}
inline void invoke(NativeConsumer original,void* manager,void* scene,void* frame,
    const Context* replacement,Context (*read)(),void (*apply)(const Context&)) {
    render_core::CallState<Context> scope(read,apply,replacement?*replacement:Context{});
    original(manager,scene,frame); // Rejections pass all three inputs exactly once.
}
} // namespace w3vr::world_culling_view
