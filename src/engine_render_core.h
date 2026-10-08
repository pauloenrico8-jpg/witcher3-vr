#pragma once

#include "engine_camera_temporal.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>

namespace w3vr::render_core {
// Native core entry/CPU call-label contract only. Does not authorize camera
// writes, deferred tasks, resource lifetime, GPU completion or game compatibility.
struct Profile { std::uintptr_t entry, normal_return; };
inline constexpr Profile legacy_404{0x01D86400, 0x01D002CF};
inline constexpr Profile remastered_500c{0x01C13630, 0x01D05551};
inline constexpr std::uintptr_t normal_epilogue_rva = 0x01D57F90;
inline constexpr std::uintptr_t normal_epilogue_vtable_rva = 0x037A6C40;
inline constexpr std::array<std::uint8_t, 16> epilogue_entry_signature{
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
inline constexpr std::array<std::uint8_t, 16> modern_entry_signature{
    0x48,0x8B,0xC4,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x56};
inline const Profile* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return &legacy_404;
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    return nullptr;
}
inline bool modern_prefix_matches(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < modern_entry_signature.size()) return false;
    for (std::size_t i = 0; i < modern_entry_signature.size(); ++i)
        if (bytes[i] != modern_entry_signature[i]) return false;
    return true;
}

// Registry metadata copied under its existing mutex. Its address is not read
// here; a matching label does not prove that a native allocation is still alive.
struct FrameLabel {
    std::uintptr_t frame{};
    std::uint32_t generation{};
    std::uint64_t pair{};
    int eye{-1};
    bool view_valid{}, normal_factory_lineage{};
};
inline bool accepts_frame_identity(std::uintptr_t frame,
    const FrameLabel* label, std::uint32_t current_generation) {
    return frame != 0 && label && label->frame == frame &&
        label->generation != 0 && label->generation == current_generation &&
        label->pair != 0 && label->pair != UINT64_MAX &&
        label->eye >= 0 && label->eye <= 1 && label->view_valid &&
        label->normal_factory_lineage;
}
inline bool accepts_frame_label(std::uintptr_t renderer, std::uintptr_t frame,
    const FrameLabel* label, std::uint32_t current_generation) {
    return renderer != 0 && accepts_frame_identity(frame,label,current_generation);
}
inline bool accepts_label(const engine_camera::TemporalContract* contract,
    std::uintptr_t caller, std::uintptr_t renderer, std::uintptr_t frame,
    const FrameLabel* label, std::uint32_t current_generation) {
    return selected(contract) == &remastered_500c &&
        caller == remastered_500c.normal_return &&
        accepts_frame_label(renderer, frame, label, current_generation);
}

// This is a copied prefix while a native task method is executing. Its frame
// pointer is only a registry key, not retained native/COM ownership. The exact
// primary RTTI table identifies the normal epilogue; UberSample and unknown
// tables are not admitted. Both emitter branches write frame at +28, renderer
// at +10. R13 is reused in the emitter; +28 is NOT its earlier descriptor.
struct TaskRecord { std::uintptr_t renderer{}, frame{}; };
inline bool read_epilogue_record(std::span<const std::uint8_t> prefix,
    std::uintptr_t module, TaskRecord& result) {
    if (prefix.size() < 0x30 || module == 0 ||
        module > std::numeric_limits<std::uintptr_t>::max() - normal_epilogue_vtable_rva)
        return false;
    std::uint64_t table{}, renderer{}, frame{};
    std::memcpy(&table, prefix.data(), 8);
    std::memcpy(&renderer, prefix.data() + 0x10, 8);
    std::memcpy(&frame, prefix.data() + 0x28, 8);
    if (table != module + normal_epilogue_vtable_rva || renderer == 0 || frame == 0)
        return false;
    result = {static_cast<std::uintptr_t>(renderer), static_cast<std::uintptr_t>(frame)};
    return true;
}
#if defined(_MSC_VER)
using NativeCore = void(__fastcall*)(void*, void*, void*);
using NativeEpilogue = void(__fastcall*)(void*);
#else
using NativeCore = void(*)(void*, void*, void*);
using NativeEpilogue = void(*)(void*);
#endif

// State belongs to the calling thread and lasts only through the original
// synchronous core call. Rejected/unknown nested calls mask the outer label.
// Read/apply are non-throwing CPU state accessors supplied by the caller.
// C++ unwinding restores state; no native SEH or DLL-unload barrier is promised.
template<class State> class CallState {
    State previous_;
    void (*apply_)(const State&);
public:
    CallState(State (*read)(), void (*apply)(const State&), const State& replacement)
        : previous_(read()), apply_(apply) { apply_(replacement); }
    ~CallState() { apply_(previous_); }
    CallState(const CallState&) = delete;
    CallState& operator=(const CallState&) = delete;
};
template<class State> void invoke(const engine_camera::TemporalContract* contract,
    std::uintptr_t caller, NativeCore original, void* renderer, void* frame,
    void* scene, const FrameLabel* label, std::uint32_t current_generation,
    const State& labelled_state, const State& neutral_state,
    State (*read)(), void (*apply)(const State&)) {
    CallState<State> scope(read, apply,
        accepts_label(contract, caller, reinterpret_cast<std::uintptr_t>(renderer),
            reinterpret_cast<std::uintptr_t>(frame), label, current_generation)
        ? labelled_state : neutral_state);
    // All three original arguments, including an optional NULL scene, pass
    // exactly once. No legacy matrix/descriptor edits or completion publication.
    original(renderer, frame, scene);
}
template<class State> void invoke_epilogue(const engine_camera::TemporalContract* contract,
    NativeEpilogue original, void* task, const TaskRecord* record,
    const FrameLabel* label, std::uint32_t current_generation,
    const State& labelled_state, const State& neutral_state,
    State (*read)(), void (*apply)(const State&)) {
    const bool admitted = selected(contract) == &remastered_500c && task && record &&
        accepts_frame_label(record->renderer, record->frame, label, current_generation);
    CallState<State> scope(read, apply, admitted ? labelled_state : neutral_state);
    original(task); // Native scheduler/arguments/resources/return behavior unchanged.
    // No original-core caller RVA is invented for this separate task method.
}
} // namespace w3vr::render_core
