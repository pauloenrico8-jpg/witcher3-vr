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
inline constexpr std::array<std::uint8_t, 16> epilogue_entry_signature{
    0x48,0x8B,0xC4,0x48,0x89,0x58,0x20,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
inline constexpr std::array<std::uint8_t, 16> modern_entry_signature{
    0x48,0x8B,0xC4,0x48,0x89,0x50,0x10,0x55,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x56};
struct Profile {
    std::uintptr_t entry, normal_return, epilogue_entry, epilogue_table;
    std::array<std::uint8_t, 16> core_prefix, task_prefix;
};
inline constexpr Profile legacy_404{0x01D86400, 0x01D002CF, 0, 0, {}, {}};
inline constexpr Profile remastered_500c{0x01C13630, 0x01D05551,
    0x01D57F90, 0x037A6C40, modern_entry_signature, epilogue_entry_signature};
inline constexpr Profile remastered_1048522{0x01C1A210, 0x01D0C5E1,
    0x01D5EFC0, 0x037B1998, modern_entry_signature, epilogue_entry_signature};
// Existing bundle still installs ONLY the independently ported 500c targets.
inline constexpr std::uintptr_t normal_epilogue_rva = remastered_500c.epilogue_entry;
inline constexpr std::uintptr_t normal_epilogue_vtable_rva = remastered_500c.epilogue_table;
inline const Profile* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::legacy_404) return &legacy_404;
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    if (contract == &engine_camera::remastered_1048522) return &remastered_1048522;
    return nullptr;
}
inline const Profile* modern_selected(const engine_camera::TemporalContract* contract) {
    const auto* profile = selected(contract);
    return profile == &remastered_500c || profile == &remastered_1048522 ? profile : nullptr;
}
enum class InstallRoute { unsupported, legacy, remastered_500c_bundle };
inline InstallRoute install_route(const Profile* profile) {
    if (profile == &legacy_404) return InstallRoute::legacy;
    if (profile == &remastered_500c) return InstallRoute::remastered_500c_bundle;
    // The new core profile alone cannot authorize the other four bundle hooks.
    return InstallRoute::unsupported;
}
inline bool ready_for(const engine_camera::TemporalContract* contract,
    const Profile* installed_profile, bool observers_ready) {
    const auto* profile = modern_selected(contract);
    return observers_ready && profile && profile == installed_profile;
}
inline bool prefix_matches(std::span<const std::uint8_t> bytes,
    const std::array<std::uint8_t, 16>& expected) {
    if (bytes.size() < expected.size()) return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (bytes[i] != expected[i]) return false;
    return true;
}
inline bool modern_prefix_matches(const engine_camera::TemporalContract* contract,
    std::span<const std::uint8_t> bytes) {
    const auto* profile = modern_selected(contract);
    return profile && prefix_matches(bytes, profile->core_prefix);
}
inline bool task_prefix_matches(const engine_camera::TemporalContract* contract,
    std::span<const std::uint8_t> bytes) {
    const auto* profile = modern_selected(contract);
    return profile && prefix_matches(bytes, profile->task_prefix);
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
    const auto* profile = modern_selected(contract);
    return profile && caller == profile->normal_return &&
        accepts_frame_label(renderer, frame, label, current_generation);
}

// This is a copied prefix while a native task method is executing. Its frame
// pointer is only a registry key, not retained native/COM ownership. The exact
// primary RTTI table identifies the normal epilogue; UberSample and unknown
// tables are not admitted. Both emitter branches write frame at +28, renderer
// at +10. R13 is reused in the emitter; +28 is NOT its earlier descriptor.
struct TaskRecord {
    std::uintptr_t renderer{}, frame{};
    const Profile* profile{}; // Immutable identity of the profile that checked the prefix.
};
inline bool read_epilogue_record(const engine_camera::TemporalContract* contract,
    std::span<const std::uint8_t> prefix, std::uintptr_t module, TaskRecord& result) {
    const auto* profile = modern_selected(contract);
    if (!profile || prefix.size() < 0x30 || module == 0 ||
        module > std::numeric_limits<std::uintptr_t>::max() - profile->epilogue_table)
        return false;
    std::uint64_t table{}, renderer{}, frame{};
    std::memcpy(&table, prefix.data(), 8);
    std::memcpy(&renderer, prefix.data() + 0x10, 8);
    std::memcpy(&frame, prefix.data() + 0x28, 8);
    if (table != module + profile->epilogue_table || renderer == 0 || frame == 0)
        return false;
    result = {static_cast<std::uintptr_t>(renderer), static_cast<std::uintptr_t>(frame), profile};
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
    const auto* profile = modern_selected(contract);
    const bool admitted = profile && task && record && record->profile == profile &&
        accepts_frame_label(record->renderer, record->frame, label, current_generation);
    CallState<State> scope(read, apply, admitted ? labelled_state : neutral_state);
    original(task); // Native scheduler/arguments/resources/return behavior unchanged.
    // No original-core caller RVA is invented for this separate task method.
}
} // namespace w3vr::render_core
