#include "engine_view_constants_contract.h"

#include <array>
#include <cstdlib>
#include <limits>

namespace {
void require(bool value) { if (!value) std::abort(); }
template<class T, std::size_t N>
void put(std::array<std::uint8_t, N>& bytes, std::size_t offset, const T& value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
}

int main() {
    namespace c = w3vr::engine_view_constants;
    namespace e = w3vr::engine_camera;
    require(c::selected(nullptr) == nullptr);
    auto copy = e::remastered_500c;
    require(c::selected(&copy) == nullptr);
    require(c::selected(&e::legacy_404) == &c::legacy_404);
    require(c::selected(&e::remastered_500c) == &c::remastered_500c);
    require(c::legacy_reentry_supported(&e::legacy_404));
    require(!c::legacy_reentry_supported(&e::remastered_500c));
    require(!c::legacy_reentry_supported(nullptr));

    std::array<std::uint8_t, 0x124> state{};
    std::array<std::uint8_t, 0xC48> descriptor{};
    put(state, 0x6C, std::uint32_t{101});
    put(state, 0x120, std::uint32_t{202});
    put(descriptor, 0xAA4, std::uint32_t{303});
    put(descriptor, 0xC44, std::uint32_t{404});
    const auto saved_state = state;
    const auto saved_descriptor = descriptor;
    c::BuilderSnapshot b{};
    require(c::read_builder(state, descriptor, &c::legacy_404, b));
    require(b.frame_id == 303 && b.guard_frame == 101);
    require(c::read_builder(state, descriptor, &c::remastered_500c, b));
    require(b.frame_id == 404 && b.guard_frame == 202);
    const auto saved = b;
    require(!c::read_builder(std::span(state).first(0x123), descriptor, &c::remastered_500c, b));
    require(!c::read_builder(state, std::span(descriptor).first(0xC47), &c::remastered_500c, b));
    require(!c::read_builder(state, descriptor, nullptr, b));
    auto layout_copy = c::remastered_500c;
    require(!c::read_builder(state, descriptor, &layout_copy, b));
    require(b.frame_id == saved.frame_id && b.guard_frame == saved.guard_frame);
    require(state == saved_state && descriptor == saved_descriptor);

    std::array<std::uint8_t, c::remastered_constants_bytes> bytes{};
    put(bytes, 8, c::constants_type);
    put(bytes, 0x18, std::uint64_t{2});
    put(bytes, 0x160, std::array<float, 2>{0.25f, -0.5f});
    put(bytes, 0x168, std::array<float, 2>{1.0f, 2.0f});
    // Old jitter/reset offsets deliberately contain unrelated values.
    put(bytes, 0x140, std::array<float, 2>{55.0f, 66.0f});
    bytes[0x19F] = 1;
    const auto saved_bytes = bytes;
    c::ConstantsSnapshot constants{};
    require(c::read_remastered_constants(bytes, constants));
    require(constants.jitter[0] == 0.25f && constants.jitter[1] == -0.5f);
    require(constants.motion_vector_scale[1] == 2.0f && !constants.reset);
    require(bytes == saved_bytes);
    const auto previous = constants;
    require(!c::read_remastered_constants(std::span(bytes).first(bytes.size() - 1), constants));
    bytes[8] ^= 1;
    require(!c::read_remastered_constants(bytes, constants));
    bytes = saved_bytes;
    put(bytes, 0x18, std::uint64_t{3});
    require(!c::read_remastered_constants(bytes, constants));
    bytes = saved_bytes;
    bytes[0x1BF] = 2; // Streamline eInvalid is not a usable reset flag.
    require(!c::read_remastered_constants(bytes, constants));
    bytes = saved_bytes;
    put(bytes, 0x168, std::numeric_limits<float>::quiet_NaN());
    require(!c::read_remastered_constants(bytes, constants));
    require(constants.jitter == previous.jitter && constants.reset == previous.reset);
    bytes = saved_bytes;
    bytes[0x1BF] = 1;
    require(c::read_remastered_constants(bytes, constants) && constants.reset);

    c::Observation observed{state.data(), descriptor.data(), 42, 7, 404, 0};
    const auto accepts = [&](const c::Observation& o, std::uintptr_t site,
        std::uint64_t pair, std::uint32_t generation, int eye, int result, bool valid) {
        return c::receipt_allowed(&e::remastered_500c, o, site,
            pair, generation, eye, result, valid);
    };
    require(accepts(observed, c::remastered_constants_return, 42, 7, 0, 0, true));
    require(!accepts(observed, 0x01B7A5E4, 42, 7, 0, 0, true)); // CALL is not RETURN.
    require(!accepts(observed, c::remastered_constants_return, 41, 7, 0, 0, true));
    require(!accepts(observed, c::remastered_constants_return, 42, 8, 0, 0, true));
    require(!accepts(observed, c::remastered_constants_return, 42, 7, 1, 0, true));
    require(!accepts(observed, c::remastered_constants_return, 42, 7, 0, 1, true));
    require(!accepts(observed, c::remastered_constants_return, 42, 7, 0, 0, false));
    require(!accepts({}, c::remastered_constants_return, 42, 7, 0, 0, true));
    require(!c::receipt_allowed(&e::legacy_404, observed,
        c::remastered_constants_return, 42, 7, 0, 0, true));
    return 0;
}
