#pragma once

#include "engine_camera_layout.h"
#include "engine_render_core.h"
#include <limits>

namespace w3vr::modern_vegetation_view {
// Adapt the early and late normal SpeedTree view updates, not every world-visibility
// consumer. A borrowed native frame/scene is usable only during its labelled
// synchronous core call. This is not native ownership or a worker-task lease.
inline constexpr std::uintptr_t update_rva = 0x014A5D50;
inline constexpr std::array<std::uint8_t,16> entry_signature{
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48,0x89,0x70,0x20,0x55,0x57,0x41,0x56,0x48};
inline constexpr std::uintptr_t preselection_rva = 0x01CE5970;
inline constexpr std::uintptr_t preselection_return = 0x01C14538;
inline constexpr std::array<std::uint8_t,16> preselection_signature{
    0x48,0x8B,0xC4,0x48,0x89,0x50,0x10,0x55,0x53,0x57,0x41,0x57,0x48,0x8D,0x6C,0x24};
inline constexpr std::size_t scene_container_offset = 0x188;
inline constexpr std::size_t frame_descriptor_offset = 0x10;
inline constexpr std::size_t camera_bytes = engine_camera_layout::remastered_500c.camera_bytes;
inline constexpr std::size_t frame_primary_offset = 0x20;
inline constexpr std::size_t frame_secondary_offset = 0x600;

enum class Route { unknown, ordinary_projection, renderer_projection };
inline Route route(std::uintptr_t caller) {
    if (caller == 0x01C14D43 || caller == 0x01CE6345) return Route::ordinary_projection;
    if (caller == 0x01C14D79 || caller == 0x01CE6379) return Route::renderer_projection;
    return Route::unknown;
}
inline std::size_t projection_offset(Route selected) {
    return selected == Route::ordinary_projection ? 0x180 : 0x340;
}
inline bool add(std::uintptr_t base,std::size_t offset,std::uintptr_t& result) {
    if (!base || base > std::numeric_limits<std::uintptr_t>::max()-offset) return false;
    result=base+offset;return true;
}
inline bool receiver_matches(Route selected,std::uintptr_t container,std::uintptr_t receiver) {
    if (selected == Route::unknown) return false;
    std::uintptr_t expected{};
    return add(container,selected == Route::ordinary_projection ? 0x4BC : 0x794,expected) &&
        receiver == expected;
}
enum class Phase { normal_core, preselection };
struct Context {
    std::uintptr_t renderer{},frame{},scene{};
    render_core::FrameLabel label{};
    bool normal_core{};
    Phase phase{Phase::normal_core};
    std::uintptr_t early_container{};
};
inline bool accepts(const engine_camera::TemporalContract* contract,const Context& context,
    std::uint32_t generation) {
    return contract == &engine_camera::remastered_500c && context.normal_core && context.scene &&
        render_core::accepts_frame_label(context.renderer,context.frame,&context.label,generation);
}
// The early updates require their own exact parent-call scope. A thread label
// alone must not grant the same authority to a private/reentrant preselection.
inline bool allows_update(const Context& context,std::uintptr_t caller,std::uintptr_t container) {
    if (context.phase == Phase::normal_core)
        return caller == 0x01C14D43 || caller == 0x01C14D79;
    return context.phase == Phase::preselection && context.early_container != 0 &&
        container == context.early_container && (caller == 0x01CE6345 || caller == 0x01CE6379);
}
inline bool bind_preselection(const engine_camera::TemporalContract* contract,const Context& outer,
    std::uint32_t generation,std::uintptr_t caller,std::uintptr_t container,
    std::uintptr_t descriptor,std::uintptr_t scene,std::uintptr_t observed_container,Context& result) {
    std::uintptr_t expected{};
    if (!accepts(contract,outer,generation) || outer.phase != Phase::normal_core ||
        caller != preselection_return || scene != outer.scene || !container ||
        container != observed_container || !add(outer.frame,frame_descriptor_offset,expected) ||
        descriptor != expected) return false;
    Context candidate=outer;candidate.phase=Phase::preselection;candidate.early_container=container;
    result=candidate;return true;
}
#if defined(_MSC_VER)
using NativePreselection=void(__fastcall*)(void*,void*,void*);
#else
using NativePreselection=void(*)(void*,void*,void*);
#endif
inline void invoke_preselection(NativePreselection original,void* container,void* descriptor,void* scene,
    const Context* replacement,Context (*read)(),void (*apply)(const Context&)) {
    render_core::CallState<Context> scope(read,apply,replacement ? *replacement : Context{});
    original(container,descriptor,scene); // Three arguments pass once, including rejected NULL inputs.
    // Native work, cache objects, waits, references and scheduling remain native.
}
struct alignas(16) Inputs {
    alignas(16) std::array<float,3> position{};
    alignas(16) std::array<float,16> projection{};
    alignas(16) std::array<float,16> view{};
    float near_range{};
};
inline bool finite(const Inputs& inputs) {
    for (float v:inputs.position) if (!std::isfinite(v)) return false;
    for (float v:inputs.projection) if (!std::isfinite(v)) return false;
    for (float v:inputs.view) if (!std::isfinite(v)) return false;
    return std::isfinite(inputs.near_range) && inputs.near_range>0;
}
inline bool read_inputs(std::span<const std::uint8_t> camera,Route selected,Inputs& result) {
    if (selected == Route::unknown || camera.size()<camera_bytes) return false;
    engine_camera::TemporalInputs source{};
    if (!engine_camera::read_temporal_inputs(camera,0,source) || source.fov>=180 ||
        source.projection_scale<=0) return false;
    Inputs candidate{};
    std::memcpy(candidate.position.data(),camera.data(),sizeof(candidate.position));
    std::memcpy(candidate.projection.data(),camera.data()+projection_offset(selected),sizeof(candidate.projection));
    std::memcpy(candidate.view.data(),camera.data()+0x40,sizeof(candidate.view));
    std::memcpy(&candidate.near_range,camera.data()+0x30,sizeof(candidate.near_range));
    if (!finite(candidate)) return false;
    result=candidate;return true;
}
inline bool same(const Inputs& a,const Inputs& b) {
    // Compare the exact native float words, never padding or numeric tolerances.
    return std::memcmp(a.position.data(),b.position.data(),sizeof(a.position))==0 &&
        std::memcmp(a.projection.data(),b.projection.data(),sizeof(a.projection))==0 &&
        std::memcmp(a.view.data(),b.view.data(),sizeof(a.view))==0 &&
        std::memcmp(&a.near_range,&b.near_range,sizeof(a.near_range))==0;
}
inline bool prepare(const engine_camera::TemporalContract* contract,const Context& context,
    std::uint32_t generation,std::uintptr_t caller,std::uintptr_t container,
    std::uintptr_t receiver,const Inputs& incoming,bool final_mode,
    std::span<const std::uint8_t> primary,std::span<const std::uint8_t> secondary,Inputs& result) {
    const Route selected=route(caller);
    if (!accepts(contract,context,generation) || !allows_update(context,caller,container) ||
        !receiver_matches(selected,container,receiver) ||
        final_mode) return false;
    Inputs expected{},candidate{};
    if (!read_inputs(secondary,selected,expected) || !same(expected,incoming) ||
        !read_inputs(primary,selected,candidate)) return false;
    // These values belong to the PRIMARY camera of this exact eye/frame. They
    // replace the secondary's position, view, projection variant and near range.
    // The native receiver, grid far range, mode and boolean result stay native.
    result=candidate;return true;
}
#if defined(_MSC_VER)
using NativeUpdate=bool(__fastcall*)(void*,const float*,const float*,const float*,float,float,bool);
#else
using NativeUpdate=bool(*)(void*,const float*,const float*,const float*,float,float,bool);
#endif
inline bool invoke(NativeUpdate original,void* receiver,const float* position,
    const float* projection,const float* view,float near_range,float far_range,
    bool final_mode,const Inputs* replacement) {
    if (replacement) return original(receiver,replacement->position.data(),replacement->projection.data(),
        replacement->view.data(),replacement->near_range,far_range,final_mode);
    return original(receiver,position,projection,view,near_range,far_range,final_mode);
    // Local input arrays survive the synchronous original call. Native shared
    // cache isolation, deferred consumption and GPU completion are not proved.
}
} // namespace w3vr::modern_vegetation_view
