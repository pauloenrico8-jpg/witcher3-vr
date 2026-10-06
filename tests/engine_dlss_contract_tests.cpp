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
    return 0;
}
