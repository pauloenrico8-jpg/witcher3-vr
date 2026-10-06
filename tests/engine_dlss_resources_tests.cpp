#include "engine_dlss_resources.h"

#include <cstdlib>

namespace {
namespace r = w3vr::engine_dlss_resources;
namespace d = w3vr::engine_dlss;
void require(bool value) { if (!value) std::abort(); }
template<class T, std::size_t N>
void put(std::array<std::uint8_t, N>& b, std::size_t offset, const T& value) {
    std::memcpy(b.data() + offset, &value, sizeof(value));
}
template<std::size_t N>
std::array<std::uint8_t, N> structure(const std::array<std::uint32_t, 4>& type) {
    std::array<std::uint8_t, N> b{};
    put(b, 8, type); put(b, 0x18, std::uint64_t{1});
    return b;
}

struct Pass {
    d::Scope scope{};
    d::Viewport viewport{0x5531D}; // Actual odd native id, never OR with eye.
    r::CpuTags tags{};
    std::array<r::Binding, 4> bindings{};
    const void* token{reinterpret_cast<void*>(0x876543210000ull)};
    void* command{reinterpret_cast<void*>(0x765432100000ull)};
    const void* input{};
    std::uint64_t serial{20};
    Pass() {
        scope.pipeline = reinterpret_cast<void*>(0x100000010ull);
        scope.descriptor = reinterpret_cast<void*>(0x100000020ull);
        scope.state = reinterpret_cast<void*>(0x100000030ull);
        scope.resources = reinterpret_cast<void*>(0x100000040ull);
        scope.snapshot = {6, 61, 73, 0};
        scope.identity = {901, 42, 0};
        scope.index0 = 0xabcdef01; scope.index2 = 0xdeadbeef;
        scope.viewport = viewport.id; scope.stage = d::Stage::evaluation; scope.valid = true;
        input = &viewport;
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            auto& b = bindings[i];
            b.tag = {0x100000100ull + i * 0x100, static_cast<std::uint32_t>(i < 2 ? i : i + 1),
                1, {0, 0, 1280, 720}};
            b.resource.native = 0x100100000ull + i * 0x100;
            b.resource.state = i == 1 ? 0 : i == 3 ? 8 : 0x40;
        }
        bindings[3].tag.extent = {0, 0, 1920, 1080};
        tags = r::begin(scope);
    }
    void tag(std::size_t i, int result = 0, std::uintptr_t caller = 0x01ED2ADA) {
        r::observe(tags, scope, scope.identity, viewport, &bindings[i],
            {token, &viewport, &bindings[i], 1, command}, caller, result, ++serial);
    }
    void all() { for (std::size_t i = 0; i < 4; ++i) tag(i); }
    r::CpuResourceReceipt evaluate(int result = 0, std::uintptr_t caller = 0x01ED3230,
        std::uint32_t feature = d::dlss) {
        return r::evaluate(tags, scope, scope.identity, viewport,
            {feature, token, &input, 1, command}, caller, result, ++serial);
    }
};
}

int main() {
    static_assert(!r::CpuResourceReceipt::gpu_completion_verified);
    static_assert(!r::CpuResourceReceipt::resource_ownership_verified);
    require(!d::stereo_reentry_verified);
    auto resource = structure<r::resource_bytes>(r::resource_type);
    put(resource, 0x28, std::uint64_t{0x765432100000ull});
    put(resource, 0x40, std::uint32_t{8});
    const auto saved_resource = resource;
    r::Resource parsed_resource{};
    require(r::read_resource(resource, &d::remastered_500c, parsed_resource));
    require(parsed_resource.native == 0x765432100000ull && parsed_resource.state == 8);
    auto tag = structure<r::tag_bytes>(r::tag_type);
    put(tag, 0x20, std::uint64_t{0x100000100ull});
    put(tag, 0x28, std::uint32_t{4}); put(tag, 0x2C, std::uint32_t{1});
    put(tag, 0x30, r::Extent{0, 0, 1920, 1080});
    const auto saved_tag = tag;
    r::Tag parsed_tag{};
    require(r::read_tag(tag, &d::remastered_500c, parsed_tag));
    require(parsed_tag.type == 4 && parsed_tag.lifecycle == 1 && parsed_tag.extent.width == 1920);
    const auto good_tag = parsed_tag;
    const auto good_resource = parsed_resource;
    auto copied_layout = d::remastered_500c;
    require(!r::read_tag(tag, &copied_layout, parsed_tag));
    require(!r::read_resource(resource, nullptr, parsed_resource));
    require(!r::read_tag(std::span(tag).first(r::tag_bytes - 1), &d::remastered_500c, parsed_tag));
    require(!r::read_resource(std::span(resource).first(r::resource_bytes - 1), &d::remastered_500c, parsed_resource));
    // Unknown chains/versions/types must pass through the SDK, but cannot
    // supply resource evidence. Failure must preserve the caller's output.
    for (auto offset : {0u, 8u, 0x18u}) {
        resource = saved_resource; resource[offset] ^= 2;
        require(!r::read_resource(resource, &d::remastered_500c, parsed_resource));
        tag = saved_tag; tag[offset] ^= 2;
        require(!r::read_tag(tag, &d::remastered_500c, parsed_tag));
    }
    for (auto offset : {0x20u, 0x30u, 0x38u}) {
        resource = saved_resource; resource[offset] = 1;
        require(!r::read_resource(resource, &d::remastered_500c, parsed_resource));
    }
    for (auto native : {std::uint64_t{0}, std::uint64_t{0xFFFF}, std::uint64_t{0x800000000000ull}, UINT64_MAX}) {
        resource = saved_resource; put(resource, 0x28, native);
        require(!r::read_resource(resource, &d::remastered_500c, parsed_resource));
    }
    resource = saved_resource; put(resource, 0x40, UINT32_MAX);
    require(!r::read_resource(resource, &d::remastered_500c, parsed_resource));
    tag = saved_tag; put(tag, 0x2C, std::uint32_t{3});
    require(!r::read_tag(tag, &d::remastered_500c, parsed_tag));
    tag = saved_tag; put(tag, 0x30, r::Extent{UINT32_MAX, 0, 2, 1});
    require(!r::read_tag(tag, &d::remastered_500c, parsed_tag));
    require(parsed_tag.resource == good_tag.resource && parsed_tag.extent == good_tag.extent);
    require(parsed_resource.native == good_resource.native && parsed_resource.state == good_resource.state);
    tag = saved_tag; put(tag, 0x20, std::uint64_t{0});
    require(r::read_tag(tag, &d::remastered_500c, parsed_tag) && parsed_tag.resource == 0);
    // CPU pointer copies never dereference the opaque token or native COM
    // addresses. Both are deliberately unmapped numeric sentinels here.
    for (int eye : {0, 1}) {
        Pass p; p.scope.identity.eye = eye; p.tags = r::begin(p.scope); p.all();
        const auto receipt = p.evaluate();
        require(receipt.valid && receipt.identity.eye == eye && receipt.viewport == 0x5531D);
        require(receipt.command == p.command && receipt.token == p.token);
        require(receipt.bindings[3].resource.native == p.bindings[3].resource.native);
        require(!p.tags.active && !p.evaluate().valid);
    }
    for (int missing = 0; missing < 4; ++missing) {
        Pass p; for (int i = 0; i < 4; ++i) if (i != missing) p.tag(i);
        require(!p.evaluate().valid && !p.tags.active);
    }
    // A partial set from one eye/frame/state/command cannot be completed
    // using values from another, even when the numeric viewport is equal.
    for (int mutation = 0; mutation < 11; ++mutation) {
        Pass p; p.tag(0);
        switch (mutation) {
        case 0: p.scope.identity.eye = 1; break;
        case 1: ++p.scope.identity.generation; break;
        case 2: ++p.scope.identity.pair_id; break;
        case 3: ++p.scope.snapshot.render_counter; break;
        case 4: ++p.scope.snapshot.token_index; break;
        case 5: p.scope.state = p.scope.descriptor; break;
        case 6: p.scope.resources = p.scope.descriptor; break;
        case 7: p.scope.descriptor = p.scope.state; break;
        case 8: p.token = p.scope.state; break;
        case 9: p.command = const_cast<void*>(p.scope.state); break;
        case 10: ++p.serial; break; // Intervening tag/eval on another thread.
        }
        for (int i = 1; i < 4; ++i) p.tag(i);
        require(!p.evaluate().valid);
    }
    for (int mutation = 0; mutation < 9; ++mutation) {
        Pass p;
        switch (mutation) {
        case 0: p.bindings[1].tag.resource = 0; break; // Explicit removal.
        case 1: p.bindings[1].tag.lifecycle = 0; break;
        case 2: p.bindings[1].tag.lifecycle = 2; break;
        case 3: p.bindings[1].tag.type = 99; break;
        case 4: p.bindings[1].tag.extent.width = 0; break;
        case 5: p.bindings[1].tag.extent.top = 1; break;
        case 6: p.command = nullptr; break;
        case 7: p.token = nullptr; break;
        case 8: p.bindings[1].resource.state = UINT32_MAX; break;
        }
        p.all(); require(!p.evaluate().valid);
    }
    for (int mutation = 0; mutation < 15; ++mutation) {
        Pass p; p.all();
        switch (mutation) {
        case 0: p.bindings[3].resource.native = p.bindings[0].resource.native;
            p.tags.bindings[3] = p.bindings[3]; break;
        case 1: ++p.tags.bindings[1].tag.extent.width; break;
        case 2: p.tags.bindings[3].tag.extent.width = 1; break;
        case 3: p.tags.bindings[1].resource.state = 8; break;
        case 4: p.tags.bindings[3].resource.state = 0; break;
        case 5: p.tags.rejected = true; break; // Nested rendering suspended proof.
        case 6: p.viewport.id = 0x5531C; break;
        case 7: ++p.scope.index2; break;
        case 8: p.scope.valid = false; break;
        case 9: p.scope.stage = d::Stage::preparation; break;
        case 10: p.token = nullptr; break;
        case 11: p.command = nullptr; break;
        case 12: ++p.serial; break; // Intervening SDK activity before eval.
        case 13: p.scope.snapshot.mode = 7; break;
        case 14: p.scope.snapshot.ray_reconstruction = 1; break;
        }
        require(!p.evaluate().valid && !p.tags.active);
    }
    { Pass p; p.all(); p.tag(1); require(!p.evaluate().valid); }
    { Pass p; p.tag(0, 17); p.all(); require(!p.evaluate().valid); }
    { Pass p; p.tag(0, 0, 0x01ED2ADB); p.all(); require(!p.evaluate().valid); }
    { Pass p; p.all(); require(!p.evaluate(-1).valid); }
    { Pass p; p.all(); require(!p.evaluate(0, 0x01ED3231).valid); }
    { Pass p; p.all(); require(!p.evaluate(0, 0x01ED3F90, d::ray_reconstruction).valid); }
    { Pass p; p.scope.snapshot.mode = 7; require(!r::begin(p.scope).active); }
    { Pass p; p.scope.snapshot.ray_reconstruction = 1; require(!r::begin(p.scope).active); }
    { Pass p; p.scope.identity.pair_id = 0; require(!r::begin(p.scope).active); }
    require(saved_resource[0x20] == 0 && saved_tag[0x28] == 4);
}
