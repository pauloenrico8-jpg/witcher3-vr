#pragma once

#include "engine_camera_temporal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace w3vr::engine_dlss {

// These fields are independently observed in the native 5.00c methods.
// The render counter and the Streamline token index are different fields;
// builder+120 uses C44, whereas evaluation+124 and preparation+128 use C40.
// This read-only contract does NOT authorize rearming any of those guards.
struct Layout {
    std::size_t descriptor_render_counter;
    std::size_t descriptor_token_index;
    std::size_t state_evaluated_counter;
    std::size_t state_prepared_counter;
};
inline constexpr Layout remastered_500c{0xC40, 0xC44, 0x124, 0x128};

inline const Layout* selected(const engine_camera::TemporalContract* contract) {
    return contract == &engine_camera::remastered_500c ? &remastered_500c : nullptr;
}

struct Counters {
    std::uint32_t render_counter{};
    std::uint32_t token_index{};
    std::uint32_t evaluated_counter{};
    std::uint32_t prepared_counter{};
};

inline bool read_counters(std::span<const std::uint8_t> state,
    std::span<const std::uint8_t> descriptor, const Layout* layout, Counters& result) {
    if (layout != &remastered_500c || state.size() < 0x12C || descriptor.size() < 0xC48)
        return false;
    Counters candidate{};
    std::memcpy(&candidate.render_counter, descriptor.data() + layout->descriptor_render_counter, 4);
    std::memcpy(&candidate.token_index, descriptor.data() + layout->descriptor_token_index, 4);
    std::memcpy(&candidate.evaluated_counter, state.data() + layout->state_evaluated_counter, 4);
    std::memcpy(&candidate.prepared_counter, state.data() + layout->state_prepared_counter, 4);
    result = candidate;
    return true;
}

// Native constructors and the public Streamline definition both identify
// this plain (non-polymorphic) ViewportHandle header and +20 value. FrameToken
// is polymorphic and opaque: this parser must NEVER be used to read a token.
inline constexpr std::array<std::uint32_t, 4> viewport_type{
    0x171B6435, 0x4FC89B3C, 0xE5FB9499, 0xA4AA6925};
inline constexpr std::size_t viewport_bytes = 0x28;
struct Viewport {
    std::uint32_t id{};
};

inline bool read_viewport(std::span<const std::uint8_t> bytes, Viewport& result) {
    if (bytes.size() < viewport_bytes) return false;
    std::uint64_t next{}, version{};
    std::array<std::uint32_t, 4> type{};
    std::memcpy(&next, bytes.data(), 8);
    std::memcpy(type.data(), bytes.data() + 8, 16);
    std::memcpy(&version, bytes.data() + 0x18, 8);
    // Unexamined chains and newer revisions still pass through the SDK, but
    // they cannot serve as evidence for our single-viewport native path.
    if (next != 0 || type != viewport_type || version != 1) return false;
    Viewport candidate{};
    std::memcpy(&candidate.id, bytes.data() + 0x20, 4);
    if (candidate.id == UINT32_MAX) return false;
    result = candidate;
    return true;
}

inline constexpr std::uint32_t dlss = 0;
inline constexpr std::uint32_t ray_reconstruction = 1001;
inline constexpr std::uintptr_t dlss_evaluate_return = 0x01ED3230;
inline constexpr std::uintptr_t ray_reconstruction_evaluate_return = 0x01ED3F90;
inline constexpr std::array<std::uintptr_t, 2> tag_returns{0x01ED2ADA, 0x01ED2BA0};

inline bool known_evaluate_return(std::uint32_t feature, std::uintptr_t site) {
    return (feature == dlss && site == dlss_evaluate_return) ||
        (feature == ray_reconstruction && site == ray_reconstruction_evaluate_return);
}
inline bool known_tag_return(std::uintptr_t site) {
    return site == tag_returns[0] || site == tag_returns[1];
}

// References in the SDK are pointers at the Windows x64 ABI. These modern
// signatures deliberately do not share the legacy Streamline 1.5 trampolines.
// The command buffer is the FIFTH argument, not the first legacy argument.
using SetTagForFrameFn = int (*)(const void* token, const void* viewport,
    const void* tags, std::uint32_t count, void* command_buffer);
using EvaluateFn = int (*)(std::uint32_t feature, const void* token,
    const void** inputs, std::uint32_t count, void* command_buffer);

struct TagCall {
    const void* token{};
    const void* viewport{};
    const void* tags{};
    std::uint32_t count{};
    void* command_buffer{};
};
struct EvaluateCall {
    std::uint32_t feature{};
    const void* token{};
    const void** inputs{};
    std::uint32_t count{};
    void* command_buffer{};
};

inline int forward(SetTagForFrameFn original, const TagCall& call) {
    return original(call.token, call.viewport, call.tags, call.count, call.command_buffer);
}
inline int forward(EvaluateFn original, const EvaluateCall& call) {
    return original(call.feature, call.token, call.inputs, call.count, call.command_buffer);
}

// The two bounded native switches lead to mode 6's actual DLSS route.
// Its caller supplies six arguments and tests AL, not the legacy seven-
// argument signature's 32-bit result. All resource indices remain opaque.
inline constexpr std::uintptr_t pipeline_entry = 0x01C02720;
inline constexpr std::uintptr_t pipeline_return = 0x01C0848E;
inline constexpr std::uintptr_t prepare_entry = 0x01B78CE0;
inline constexpr std::uintptr_t prepare_return = 0x01C03501;
inline constexpr std::uintptr_t evaluate_entry = 0x01B77EB0;
inline constexpr std::uintptr_t evaluate_return = 0x01C03550;
using PipelineFn = std::uint8_t (*)(void* pipeline, void* descriptor,
    std::uint32_t index0, std::uint32_t index1, std::uint32_t index2, std::uint8_t flag);
// The examined native caller ignores these methods' RAX. Preserve its whole
// payload without assigning it a bool, SDK status, or completion meaning.
using PrepareFn = std::uintptr_t (*)(void* state, void* descriptor, void* resources);
using NativeEvaluateFn = std::uintptr_t (*)(void* state, void* descriptor,
    void* resources, std::uint32_t index0, std::uint32_t index2);
struct PipelineCall {
    void* pipeline{};
    void* descriptor{};
    std::uint32_t index0{}, index1{}, index2{};
    std::uint8_t flag{};
};
inline std::uint8_t forward(PipelineFn original, const PipelineCall& call) {
    return original(call.pipeline, call.descriptor,
        call.index0, call.index1, call.index2, call.flag);
}
struct PrepareCall { void* state{}; void* descriptor{}; void* resources{}; };
struct NativeEvaluateCall {
    void* state{}; void* descriptor{}; void* resources{};
    std::uint32_t index0{}, index2{};
};
inline std::uintptr_t forward(PrepareFn original, const PrepareCall& call) {
    return original(call.state, call.descriptor, call.resources);
}
inline std::uintptr_t forward(NativeEvaluateFn original, const NativeEvaluateCall& call) {
    return original(call.state, call.descriptor, call.resources, call.index0, call.index2);
}

struct PipelineSnapshot {
    std::uint32_t mode{}, render_counter{}, token_index{};
    std::uint8_t ray_reconstruction{};
};
inline bool read_pipeline(std::span<const std::uint8_t> pipeline,
    std::span<const std::uint8_t> descriptor, const Layout* layout, PipelineSnapshot& result) {
    if (layout != &remastered_500c || pipeline.size() < 0x78 || descriptor.size() < 0xF750)
        return false;
    PipelineSnapshot candidate{};
    std::memcpy(&candidate.mode, pipeline.data() + 0x74, 4);
    std::memcpy(&candidate.render_counter, descriptor.data() + 0xC40, 4);
    std::memcpy(&candidate.token_index, descriptor.data() + 0xC44, 4);
    candidate.ray_reconstruction = descriptor[0xF73C];
    if (candidate.mode > 8 || candidate.ray_reconstruction > 1) return false;
    result = candidate;
    return true;
}
inline bool read_state_viewport(std::span<const std::uint8_t> state,
    const Layout* layout, Viewport& result) {
    if (layout != &remastered_500c || state.size() < 0x184) return false;
    Viewport candidate{};
    std::memcpy(&candidate.id, state.data() + 0x180, 4);
    if (candidate.id == UINT32_MAX) return false;
    result = candidate;
    return true;
}

// Static chain verified from allocator -> constructor -> global -> the
// CRenderInterface primary vtable's C0 getter. Runtime validation compares
// that chain at the callback; it does not call the getter or claim ownership.
inline constexpr std::uintptr_t renderer_global = 0x05A51950;
inline constexpr std::uintptr_t shared_state_global = 0x0584DE48;
inline constexpr std::uintptr_t renderer_vtable = 0x036E03C8;
inline constexpr std::uintptr_t shared_state_getter = 0x01BD9700;
inline constexpr std::uint32_t constructed_viewport = 0x5531D;
struct StateConnection {
    const void* renderer{};
    const void* global_state{};
    const void* observed_state{};
    std::uintptr_t vtable_rva{}, getter_rva{};
    std::uint32_t viewport{UINT32_MAX};
    std::uint8_t sdk_initialized{};
};
inline bool known_state_connection(const engine_camera::TemporalContract* contract,
    const StateConnection& connection) {
    return contract == &engine_camera::remastered_500c && connection.renderer != nullptr &&
        connection.global_state != nullptr && connection.global_state == connection.observed_state &&
        connection.vtable_rva == renderer_vtable && connection.getter_rva == shared_state_getter &&
        connection.viewport == constructed_viewport && connection.sdk_initialized == 1;
}

struct Identity {
    std::uint64_t pair_id{};
    std::uint32_t generation{};
    int eye{-1};
    bool valid() const {
        return pair_id != 0 && pair_id != UINT64_MAX && (eye == 0 || eye == 1);
    }
    bool operator==(const Identity&) const = default;
};
enum class Stage : std::uint8_t { none, preparation, evaluation };
struct Scope {
    const void* pipeline{};
    const void* descriptor{};
    const void* state{};
    const void* resources{};
    PipelineSnapshot snapshot{};
    Identity identity{};
    std::uint32_t index0{}, index2{};
    std::uint32_t viewport{UINT32_MAX};
    Stage stage{Stage::none};
    bool valid{};
};
inline Scope pipeline_scope(const engine_camera::TemporalContract* contract,
    const PipelineCall& call, std::uintptr_t caller, const PipelineSnapshot& snapshot,
    const Identity& identity) {
    if (contract != &engine_camera::remastered_500c || caller != pipeline_return ||
        call.pipeline == nullptr || call.descriptor == nullptr || !identity.valid() ||
        snapshot.mode != 6 || snapshot.ray_reconstruction != 0) return {};
    Scope scope{};
    scope.pipeline = call.pipeline;
    scope.descriptor = call.descriptor;
    scope.snapshot = snapshot;
    scope.identity = identity;
    scope.index0 = call.index0;
    scope.index2 = call.index2;
    scope.valid = true;
    return scope;
}
inline bool known_stage_return(Stage stage, std::uintptr_t caller) {
    return (stage == Stage::preparation && caller == prepare_return) ||
        (stage == Stage::evaluation && caller == evaluate_return);
}
inline bool evaluate_indices_match(const Scope& scope, std::uint32_t index0, std::uint32_t index2) {
    return scope.valid && scope.index0 == index0 && scope.index2 == index2;
}
inline bool enter_stage(const Scope& root, Stage stage, std::uintptr_t caller,
    const void* state, const void* descriptor, const void* resources,
    const Counters& counters, const Viewport& viewport, const Identity& identity, Scope& result) {
    if (!root.valid || !known_stage_return(stage, caller) || !identity.valid() ||
        root.identity != identity || state == nullptr || resources == nullptr ||
        descriptor != root.descriptor || viewport.id == UINT32_MAX ||
        counters.render_counter != root.snapshot.render_counter ||
        counters.token_index != root.snapshot.token_index ||
        (root.state != nullptr && root.state != state) ||
        (root.resources != nullptr && root.resources != resources) ||
        (root.viewport != UINT32_MAX && root.viewport != viewport.id)) return false;
    Scope candidate = root;
    candidate.state = state;
    candidate.resources = resources;
    candidate.viewport = viewport.id;
    candidate.stage = stage;
    result = candidate;
    return true;
}
inline bool scoped_sdk_call(const Scope& scope, Stage stage,
    const Identity& identity, const Viewport& viewport) {
    return scope.valid && stage != Stage::none && scope.stage == stage &&
        identity.valid() && scope.identity == identity && scope.state != nullptr &&
        scope.resources != nullptr && scope.viewport != UINT32_MAX && scope.viewport == viewport.id;
}

inline constexpr std::uintptr_t options_getter_return = 0x01ED31B5;
inline constexpr std::uintptr_t options_setter_return = 0x01ED31CA;
inline constexpr std::string_view options_function_name = "slDLSSSetOptions";
inline constexpr std::array<std::uint32_t, 4> options_type{
    0x6AC826E4, 0x41014C61, 0x8D632DA9, 0xB8571042};
inline constexpr std::size_t options_header_bytes = 0x20;
// Only the independently verified v3 header is interpreted. Options payload,
// presets, dimensions and exposure values remain wholly owned by the SDK.
inline bool known_options_header(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < options_header_bytes) return false;
    std::uint64_t next{}, version{};
    std::array<std::uint32_t, 4> type{};
    std::memcpy(&next, bytes.data(), 8);
    std::memcpy(type.data(), bytes.data() + 8, 16);
    std::memcpy(&version, bytes.data() + 0x18, 8);
    return next == 0 && type == options_type && version == 3;
}
inline bool known_options_getter(std::uint32_t feature, std::string_view name, std::uintptr_t caller) {
    return feature == dlss && name == options_function_name && caller == options_getter_return;
}
using GetFeatureFunctionFn = int (*)(std::uint32_t feature, const char* name, void** function);
using SetOptionsFn = int (*)(const void* viewport, const void* options);
struct GetFeatureFunctionCall { std::uint32_t feature{}; const char* name{}; void** function{}; };
struct SetOptionsCall { const void* viewport{}; const void* options{}; };
inline int forward(GetFeatureFunctionFn original, const GetFeatureFunctionCall& call) {
    return original(call.feature, call.name, call.function);
}
inline int forward(SetOptionsFn original, const SetOptionsCall& call) {
    return original(call.viewport, call.options);
}

// A validated SDK call is still not a receipt of an eye's render, temporal
// isolation, execution on the GPU, or completion of that execution.
inline constexpr bool stereo_reentry_verified = false;

} // namespace w3vr::engine_dlss
