#include "engine_dlss_contract.h"

#include <array>
#include <cstdlib>

namespace {
void require(bool value) { if (!value) std::abort(); }
template<class T, std::size_t N>
void put(std::array<std::uint8_t, N>& bytes, std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
namespace c = w3vr::engine_dlss;
c::TagCall seen_tag{};
c::EvaluateCall seen_evaluate{};
int calls{}, sdk_result{};
c::PipelineCall seen_pipeline{};
c::PrepareCall seen_prepare{};
c::NativeEvaluateCall seen_native_evaluate{};
std::uint8_t pipeline_result{};
std::uintptr_t method_payload{0xABCD567812345678ull};
c::GetFeatureFunctionCall seen_getter{};
c::SetOptionsCall seen_options{};
void* getter_output{};
int getter_sdk(std::uint32_t feature, const char* name, void** function) {
    seen_getter = {feature, name, function};
    if (function != nullptr) *function = getter_output;
    ++calls;
    return sdk_result;
}
int options_sdk(const void* viewport, const void* options) {
    seen_options = {viewport, options};
    ++calls;
    return sdk_result;
}
std::uint8_t pipeline_native(void* pipeline, void* descriptor,
    std::uint32_t index0, std::uint32_t index1, std::uint32_t index2, std::uint8_t flag) {
    seen_pipeline = {pipeline, descriptor, index0, index1, index2, flag};
    ++calls;
    return pipeline_result;
}
std::uintptr_t prepare_native(void* state, void* descriptor, void* resources) {
    seen_prepare = {state, descriptor, resources};
    ++calls;
    return method_payload;
}
std::uintptr_t evaluate_native(void* state, void* descriptor, void* resources,
    std::uint32_t index0, std::uint32_t index2) {
    seen_native_evaluate = {state, descriptor, resources, index0, index2};
    ++calls;
    return method_payload;
}
int tag_sdk(const void* token, const void* viewport, const void* tags,
    std::uint32_t count, void* command) {
    seen_tag = {token, viewport, tags, count, command};
    ++calls;
    return sdk_result;
}
int evaluate_sdk(std::uint32_t feature, const void* token, const void** inputs,
    std::uint32_t count, void* command) {
    seen_evaluate = {feature, token, inputs, count, command};
    ++calls;
    return sdk_result;
}
}

int main() {
    namespace e = w3vr::engine_camera;
    require(c::selected(&e::remastered_500c) == &c::remastered_500c);
    auto copied_contract = e::remastered_500c;
    require(c::selected(&copied_contract) == nullptr);
    require(c::selected(&e::legacy_404) == nullptr && c::selected(nullptr) == nullptr);
    require(!c::stereo_reentry_verified);

    // Deliberately distinct adjacent values catch C40/C44 and 120/124/128
    // substitution: those substitutions would silently skip or repeat work.
    std::array<std::uint8_t, 0x12C> state{};
    std::array<std::uint8_t, 0xC48> descriptor{};
    put(state, 0x120, std::uint32_t{777});
    put(state, 0x124, std::uint32_t{81});
    put(state, 0x128, std::uint32_t{80});
    put(descriptor, 0xC40, std::uint32_t{82});
    put(descriptor, 0xC44, std::uint32_t{778});
    const auto saved_state = state;
    const auto saved_descriptor = descriptor;
    c::Counters counters{};
    require(c::read_counters(state, descriptor, &c::remastered_500c, counters));
    require(counters.render_counter == 82 && counters.token_index == 778);
    require(counters.evaluated_counter == 81 && counters.prepared_counter == 80);
    auto copied_layout = c::remastered_500c;
    require(!c::read_counters(state, descriptor, &copied_layout, counters));
    require(!c::read_counters(state, descriptor, nullptr, counters));
    require(!c::read_counters(std::span(state).first(0x12B), descriptor, &c::remastered_500c, counters));
    require(!c::read_counters(state, std::span(descriptor).first(0xC47), &c::remastered_500c, counters));
    require(counters.render_counter == 82 && counters.prepared_counter == 80);
    require(state == saved_state && descriptor == saved_descriptor);

    std::array<std::uint8_t, c::viewport_bytes> viewport{};
    put(viewport, 8, c::viewport_type);
    put(viewport, 0x18, std::uint64_t{1});
    // A valid odd id must be preserved, not OR-ed with the eye number.
    put(viewport, 0x20, std::uint32_t{5});
    const auto saved_viewport = viewport;
    c::Viewport parsed{99};
    require(c::read_viewport(viewport, parsed) && parsed.id == 5);
    require(!c::read_viewport(std::span(viewport).first(0x27), parsed));
    put(viewport, 0x18, std::uint64_t{2});
    require(!c::read_viewport(viewport, parsed));
    viewport = saved_viewport;
    viewport[8] ^= 1;
    require(!c::read_viewport(viewport, parsed));
    viewport = saved_viewport;
    put(viewport, 0, std::uint64_t{0x100000080ull});
    require(!c::read_viewport(viewport, parsed));
    viewport = saved_viewport;
    put(viewport, 0x20, UINT32_MAX);
    require(!c::read_viewport(viewport, parsed));
    require(parsed.id == 5);
    viewport = saved_viewport;

    require(c::known_evaluate_return(c::dlss, c::dlss_evaluate_return));
    require(c::known_evaluate_return(c::ray_reconstruction, c::ray_reconstruction_evaluate_return));
    require(!c::known_evaluate_return(c::dlss, c::ray_reconstruction_evaluate_return));
    require(!c::known_evaluate_return(c::ray_reconstruction, c::dlss_evaluate_return));
    require(!c::known_evaluate_return(1000, c::dlss_evaluate_return));
    require(!c::known_evaluate_return(c::dlss, 0x01ED322A)); // CALL is not RETURN.
    require(c::known_tag_return(0x01ED2ADA) && c::known_tag_return(0x01ED2BA0));
    require(!c::known_tag_return(0x01ED2AD4));

    // The ABI harness passes opaque, deliberately non-dereferenceable 64-bit
    // addresses and verifies the fifth stack argument and full pointer array.
    // No test is allowed to infer a frame number from a token's address.
    static_assert(sizeof(void*) == 8);
    const auto* token = reinterpret_cast<const void*>(std::uintptr_t{0x123456789ABC});
    auto* command = reinterpret_cast<void*>(std::uintptr_t{0x3456789ABCDE});
    std::array<std::uint8_t, 192> tags{};
    const c::TagCall tag{token, viewport.data(), tags.data(), 3, command};
    for (int result : {0, 11, -7}) {
        sdk_result = result;
        const int before = calls;
        require(c::forward(&tag_sdk, tag) == result && calls == before + 1);
        require(seen_tag.token == token && seen_tag.viewport == viewport.data());
        require(seen_tag.tags == tags.data() && seen_tag.count == 3 && seen_tag.command_buffer == command);
    }
    std::array<const void*, 3> inputs{viewport.data(), tags.data(), descriptor.data()};
    const auto saved_inputs = inputs;
    for (std::uint32_t feature : {c::dlss, c::ray_reconstruction, std::uint32_t{9999}}) {
        const c::EvaluateCall call{feature, token, inputs.data(), 3, command};
        sdk_result = -9;
        const int before = calls;
        require(c::forward(&evaluate_sdk, call) == -9 && calls == before + 1);
        require(seen_evaluate.feature == feature && seen_evaluate.token == token);
        require(seen_evaluate.inputs == inputs.data() && seen_evaluate.count == 3);
        require(seen_evaluate.command_buffer == command && inputs == saved_inputs);
    }
    // Removal and optional-command cases also preserve the caller's values.
    sdk_result = 0;
    require(c::forward(&tag_sdk, {token, viewport.data(), nullptr, 1, nullptr}) == 0);
    require(seen_tag.tags == nullptr && seen_tag.count == 1 && seen_tag.command_buffer == nullptr);
    require(c::forward(&evaluate_sdk, {9999, token, nullptr, 0, nullptr}) == 0);
    require(seen_evaluate.inputs == nullptr && seen_evaluate.count == 0);
    require(viewport == saved_viewport && state == saved_state && descriptor == saved_descriptor);

    // Preserve the real six-argument ABI, including both stack arguments.
    auto* native_pipeline = reinterpret_cast<void*>(std::uintptr_t{0x789ABCDE1234});
    auto* native_descriptor = reinterpret_cast<void*>(std::uintptr_t{0x89ABCDE12345});
    auto* native_state = reinterpret_cast<void*>(std::uintptr_t{0x9ABCDE123456});
    auto* native_resources = reinterpret_cast<void*>(std::uintptr_t{0xABCDE1234567});
    const c::PipelineCall pipeline_call{native_pipeline, native_descriptor,
        0xFEDCBA98, 0x12345678, 0x89ABCDEF, 0xA5};
    for (std::uint8_t result : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{0xE1}}) {
        pipeline_result = result;
        const int before = calls;
        require(c::forward(&pipeline_native, pipeline_call) == result && calls == before + 1);
        require(seen_pipeline.pipeline == native_pipeline && seen_pipeline.descriptor == native_descriptor);
        require(seen_pipeline.index0 == 0xFEDCBA98 && seen_pipeline.index1 == 0x12345678);
        require(seen_pipeline.index2 == 0x89ABCDEF && seen_pipeline.flag == 0xA5);
    }
    require(c::forward(&prepare_native,
        c::PrepareCall{native_state, native_descriptor, native_resources}) == method_payload);
    require(seen_prepare.state == native_state && seen_prepare.descriptor == native_descriptor &&
        seen_prepare.resources == native_resources);
    require(c::forward(&evaluate_native,
        c::NativeEvaluateCall{native_state, native_descriptor, native_resources, 0xFEDCBA98, 0x89ABCDEF}) ==
        method_payload);
    require(seen_native_evaluate.index0 == 0xFEDCBA98 && seen_native_evaluate.index2 == 0x89ABCDEF &&
        seen_native_evaluate.state == native_state && seen_native_evaluate.descriptor == native_descriptor &&
        seen_native_evaluate.resources == native_resources);

    std::array<std::uint8_t, 0x78> pipeline_bytes{};
    std::array<std::uint8_t, 0xF750> descriptor_bytes{};
    std::array<std::uint8_t, 0x184> state_bytes{};
    put(pipeline_bytes, 0x74, std::uint32_t{6});
    put(descriptor_bytes, 0xC40, std::uint32_t{82});
    put(descriptor_bytes, 0xC44, std::uint32_t{778});
    put(state_bytes, 0x124, std::uint32_t{81});
    put(state_bytes, 0x128, std::uint32_t{80});
    put(state_bytes, 0x17C, std::uint32_t{8}); // Native option-change cooldown.
    put(state_bytes, 0x180, std::uint32_t{5});
    const auto saved_native_state = state_bytes;
    const auto saved_native_descriptor = descriptor_bytes;
    c::PipelineSnapshot pipeline_snapshot{};
    c::Viewport native_viewport{};
    require(c::read_pipeline(pipeline_bytes, descriptor_bytes, &c::remastered_500c, pipeline_snapshot));
    require(pipeline_snapshot.mode == 6 && pipeline_snapshot.render_counter == 82 &&
        pipeline_snapshot.token_index == 778 && pipeline_snapshot.ray_reconstruction == 0);
    require(!c::read_pipeline(pipeline_bytes, descriptor_bytes, &copied_layout, pipeline_snapshot));
    require(!c::read_pipeline(std::span(pipeline_bytes).first(0x77), descriptor_bytes,
        &c::remastered_500c, pipeline_snapshot));
    require(!c::read_pipeline(pipeline_bytes, std::span(descriptor_bytes).first(0xF74F),
        &c::remastered_500c, pipeline_snapshot));
    require(c::read_state_viewport(state_bytes, &c::remastered_500c, native_viewport) && native_viewport.id == 5);
    require(!c::read_state_viewport(std::span(state_bytes).first(0x183), &c::remastered_500c, native_viewport));
    require(!c::read_state_viewport(state_bytes, &copied_layout, native_viewport));
    put(state_bytes, 0x180, UINT32_MAX);
    require(!c::read_state_viewport(state_bytes, &c::remastered_500c, native_viewport) && native_viewport.id == 5);
    state_bytes = saved_native_state;
    require(c::read_counters(state_bytes, descriptor_bytes, &c::remastered_500c, counters));
    const c::Identity identity{112, 9, 0};
    const auto root = c::pipeline_scope(&e::remastered_500c, pipeline_call,
        c::pipeline_return, pipeline_snapshot, identity);
    require(root.valid && root.state == nullptr && root.stage == c::Stage::none);
    require(!c::pipeline_scope(&copied_contract, pipeline_call, c::pipeline_return, pipeline_snapshot, identity).valid);
    require(!c::pipeline_scope(&e::legacy_404, pipeline_call, c::pipeline_return, pipeline_snapshot, identity).valid);
    require(!c::pipeline_scope(&e::remastered_500c, pipeline_call, 0x01C08489, pipeline_snapshot, identity).valid);
    for (c::Identity invalid : {c::Identity{0, 9, 0}, c::Identity{UINT64_MAX, 9, 0},
        c::Identity{112, 9, -1}, c::Identity{112, 9, 2}})
        require(!c::pipeline_scope(&e::remastered_500c, pipeline_call, c::pipeline_return, pipeline_snapshot, invalid).valid);
    auto other_backend = pipeline_snapshot;
    other_backend.mode = 5;
    require(!c::pipeline_scope(&e::remastered_500c, pipeline_call, c::pipeline_return, other_backend, identity).valid);
    other_backend = pipeline_snapshot;
    other_backend.ray_reconstruction = 1;
    require(!c::pipeline_scope(&e::remastered_500c, pipeline_call, c::pipeline_return, other_backend, identity).valid);
    require(c::evaluate_indices_match(root, 0xFEDCBA98, 0x89ABCDEF));
    require(!c::evaluate_indices_match(root, 0x12345678, 0x89ABCDEF));
    require(!c::evaluate_indices_match(root, 0xFEDCBA98, 0x89ABCDEE));

    c::Scope prepared{};
    require(c::enter_stage(root, c::Stage::preparation, c::prepare_return, native_state,
        native_descriptor, native_resources, counters, native_viewport, identity, prepared));
    require(c::scoped_sdk_call(prepared, c::Stage::preparation, identity, native_viewport));
    require(!c::scoped_sdk_call(prepared, c::Stage::evaluation, identity, native_viewport));
    c::Scope evaluated{};
    require(c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return, native_state,
        native_descriptor, native_resources, counters, native_viewport, identity, evaluated));
    require(c::scoped_sdk_call(evaluated, c::Stage::evaluation, identity, native_viewport));
    for (c::Identity changed : {c::Identity{113, 9, 0}, c::Identity{112, 10, 0}, c::Identity{112, 9, 1}}) {
        require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return, native_state,
            native_descriptor, native_resources, counters, native_viewport, changed, evaluated));
        require(!c::scoped_sdk_call(prepared, c::Stage::preparation, changed, native_viewport));
    }
    require(!c::scoped_sdk_call(prepared, c::Stage::preparation, identity, {4}));
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return,
        command, native_descriptor, native_resources, counters, native_viewport, identity, evaluated));
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return,
        native_state, command, native_resources, counters, native_viewport, identity, evaluated));
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return,
        native_state, native_descriptor, command, counters, native_viewport, identity, evaluated));
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return,
        native_state, native_descriptor, native_resources, counters, {4}, identity, evaluated));
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::prepare_return,
        native_state, native_descriptor, native_resources, counters, native_viewport, identity, evaluated));
    auto changed_counters = counters;
    ++changed_counters.render_counter;
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return,
        native_state, native_descriptor, native_resources, changed_counters, native_viewport, identity, evaluated));
    changed_counters = counters;
    ++changed_counters.token_index;
    require(!c::enter_stage(prepared, c::Stage::evaluation, c::evaluate_return,
        native_state, native_descriptor, native_resources, changed_counters, native_viewport, identity, evaluated));
    require(evaluated.valid && evaluated.identity == identity && evaluated.stage == c::Stage::evaluation);
    require(state_bytes == saved_native_state && descriptor_bytes == saved_native_descriptor);
    // Invalid mode/flag and incomplete records leave the previous snapshot.
    put(pipeline_bytes, 0x74, std::uint32_t{9});
    require(!c::read_pipeline(pipeline_bytes, descriptor_bytes, &c::remastered_500c, pipeline_snapshot));
    put(pipeline_bytes, 0x74, std::uint32_t{6});
    descriptor_bytes[0xF73C] = 2;
    require(!c::read_pipeline(pipeline_bytes, descriptor_bytes, &c::remastered_500c, pipeline_snapshot));
    require(pipeline_snapshot.mode == 6 && pipeline_snapshot.ray_reconstruction == 0);

    std::array<std::uint8_t, 0x58> options{};
    put(options, 8, c::options_type);
    put(options, 0x18, std::uint64_t{3});
    // The payload is deliberately not decoded or normalized by our adapter.
    for (std::size_t i = 0x20; i < options.size(); ++i) options[i] = static_cast<std::uint8_t>(i * 7);
    const auto saved_options = options;
    require(c::known_options_header(options));
    require(!c::known_options_header(std::span(options).first(0x1F)));
    put(options, 0x18, std::uint64_t{4});
    require(!c::known_options_header(options));
    options = saved_options;
    put(options, 0, std::uint64_t{0x100000040ull});
    require(!c::known_options_header(options));
    options = saved_options;
    options[8] ^= 1;
    require(!c::known_options_header(options));
    options = saved_options;
    require(c::known_options_getter(c::dlss, "slDLSSSetOptions", c::options_getter_return));
    require(!c::known_options_getter(c::ray_reconstruction, "slDLSSSetOptions", c::options_getter_return));
    require(!c::known_options_getter(c::dlss, "slDLSSGetOptimalSettings", c::options_getter_return));
    require(!c::known_options_getter(c::dlss, "slDLSSSetOptions_extra", c::options_getter_return));
    require(!c::known_options_getter(c::dlss, "slDLSSSetOptions", 0x01ED31AF));
    const char* requested_name = "slDLSSSetOptions";
    void* function{};
    for (int result : {0, 7, -13}) {
        sdk_result = result;
        getter_output = native_resources;
        const int before = calls;
        require(c::forward(&getter_sdk, {c::dlss, requested_name, &function}) == result && calls == before + 1);
        require(seen_getter.feature == c::dlss && seen_getter.name == requested_name &&
            seen_getter.function == &function && function == native_resources);
        require(c::forward(&options_sdk, {viewport.data(), options.data()}) == result);
        require(seen_options.viewport == viewport.data() && seen_options.options == options.data());
    }
    sdk_result = -3;
    require(c::forward(&getter_sdk, {9999, nullptr, nullptr}) == -3);
    require(seen_getter.feature == 9999 && seen_getter.name == nullptr && seen_getter.function == nullptr);
    require(c::forward(&options_sdk, {nullptr, nullptr}) == -3);
    require(seen_options.viewport == nullptr && seen_options.options == nullptr);
    require(options == saved_options && viewport == saved_viewport && state_bytes == saved_native_state);
    return 0;
}
