#pragma once

#include "engine_dlss_contract.h"

namespace w3vr::engine_dlss_resources {

namespace d = engine_dlss;

// Independently checked against the 5.00c producer and its requested SDK
// 2.14.1 ABI. These are not the legacy Resource/ResourceTag layouts.
inline constexpr std::array<std::uint32_t, 4> resource_type{
    0x3A9D70CF, 0x4B722418, 0xF8139183, 0x61721C72};
inline constexpr std::array<std::uint32_t, 4> tag_type{
    0x4C6A5AAD, 0x496CB445, 0xF31AFF87, 0x53E65B84};
inline constexpr std::size_t resource_bytes = 0x70;
inline constexpr std::size_t tag_bytes = 0x40;

inline bool address(std::uint64_t value) {
    return value >= 0x10000 && value <= 0x00007FFFFFFFFFFFull;
}

inline bool header(std::span<const std::uint8_t> bytes,
    const std::array<std::uint32_t, 4>& type, std::size_t minimum) {
    if (bytes.size() < minimum) return false;
    std::uint64_t next{}, version{};
    std::array<std::uint32_t, 4> actual{};
    std::memcpy(&next, bytes.data(), 8);
    std::memcpy(actual.data(), bytes.data() + 8, 16);
    std::memcpy(&version, bytes.data() + 0x18, 8);
    return next == 0 && version == 1 && actual == type;
}

struct Extent {
    std::uint32_t left{}, top{}, width{}, height{};
    bool operator==(const Extent&) const = default;
};
struct Resource {
    // Numeric addresses are copied while the callback owns the native
    // structures. They are never dereferenced or treated as owned COM refs.
    std::uint64_t native{}, memory{}, view{};
    std::uint32_t state{UINT32_MAX};
};
struct Tag {
    std::uint64_t resource{};
    std::uint32_t type{UINT32_MAX}, lifecycle{UINT32_MAX};
    Extent extent{};
};
struct Binding { Tag tag{}; Resource resource{}; };

inline bool read_resource(std::span<const std::uint8_t> bytes,
    const d::Layout* layout, Resource& result) {
    if (layout != &d::remastered_500c || !header(bytes, resource_type, resource_bytes) ||
        bytes[0x20] != 0) return false; // Only the observed Tex2D path.
    Resource candidate{};
    std::memcpy(&candidate.native, bytes.data() + 0x28, 8);
    std::memcpy(&candidate.memory, bytes.data() + 0x30, 8);
    std::memcpy(&candidate.view, bytes.data() + 0x38, 8);
    std::memcpy(&candidate.state, bytes.data() + 0x40, 4);
    if (!address(candidate.native) || candidate.memory != 0 || candidate.view != 0 ||
        candidate.state == UINT32_MAX) return false;
    result = candidate;
    return true;
}

inline bool read_tag(std::span<const std::uint8_t> bytes,
    const d::Layout* layout, Tag& result) {
    if (layout != &d::remastered_500c || !header(bytes, tag_type, tag_bytes)) return false;
    Tag candidate{};
    std::memcpy(&candidate.resource, bytes.data() + 0x20, 8);
    std::memcpy(&candidate.type, bytes.data() + 0x28, 4);
    std::memcpy(&candidate.lifecycle, bytes.data() + 0x2C, 4);
    std::memcpy(&candidate.extent, bytes.data() + 0x30, sizeof(Extent));
    if ((candidate.resource != 0 && !address(candidate.resource)) ||
        candidate.type == UINT32_MAX || candidate.lifecycle > 2 ||
        candidate.extent.left > UINT32_MAX - candidate.extent.width ||
        candidate.extent.top > UINT32_MAX - candidate.extent.height) return false;
    result = candidate;
    return true;
}

inline int slot(std::uint32_t type) {
    // Depth, motion vectors, input colour, output colour.
    switch (type) { case 0: return 0; case 1: return 1; case 3: return 2; case 4: return 3; }
    return -1;
}

inline bool same_scope(const d::Scope& a, const d::Scope& b) {
    return a.valid && b.valid && a.stage == d::Stage::evaluation && b.stage == a.stage &&
        a.identity == b.identity && a.pipeline == b.pipeline &&
        a.descriptor == b.descriptor && a.state == b.state && a.resources == b.resources &&
        a.viewport == b.viewport && a.viewport != UINT32_MAX &&
        a.snapshot.mode == b.snapshot.mode && a.snapshot.ray_reconstruction == b.snapshot.ray_reconstruction &&
        a.snapshot.render_counter == b.snapshot.render_counter &&
        a.snapshot.token_index == b.snapshot.token_index &&
        a.index0 == b.index0 && a.index2 == b.index2;
}

struct CpuTags {
    d::Scope scope{};
    const void* token{}; // Opaque: compare addresses, never read FrameToken.
    void* command{};
    std::array<Binding, 4> bindings{};
    std::uint64_t last_activity_serial{};
    std::uint8_t mask{};
    bool active{}, rejected{};
};

inline CpuTags begin(const d::Scope& scope) {
    CpuTags result{};
    if (!same_scope(scope, scope) || !scope.identity.valid() ||
        scope.snapshot.mode != 6 || scope.snapshot.ray_reconstruction != 0 ||
        scope.state == nullptr || scope.resources == nullptr || scope.pipeline == nullptr ||
        scope.descriptor == nullptr) return result;
    result.scope = scope;
    result.active = true;
    return result;
}

inline void observe(CpuTags& tags, const d::Scope& scope, const d::Identity& identity,
    const d::Viewport& viewport, const Binding* binding, const d::TagCall& call,
    std::uintptr_t return_rva, int sdk_result, std::uint64_t activity_serial) {
    if (!tags.active || tags.rejected) return;
    const int index = binding != nullptr ? slot(binding->tag.type) : -1;
    const bool native = d::known_tag_return(return_rva) && call.count == 1 && call.tags != nullptr &&
        same_scope(tags.scope, scope) &&
        d::scoped_sdk_call(scope, d::Stage::evaluation, identity, viewport);
    if (!native || binding == nullptr || sdk_result != 0 || index < 0 ||
        call.token == nullptr || call.command_buffer == nullptr || binding->tag.resource == 0 ||
        binding->tag.lifecycle != 1 || !address(binding->resource.native) ||
        binding->resource.memory != 0 || binding->resource.view != 0 ||
        binding->resource.state == UINT32_MAX || binding->tag.extent.left != 0 ||
        binding->tag.extent.top != 0 || binding->tag.extent.width == 0 ||
        binding->tag.extent.height == 0 ||
        (tags.mask & (1u << index)) != 0 ||
        (tags.mask != 0 && activity_serial != tags.last_activity_serial + 1) ||
        (tags.token != nullptr && tags.token != call.token) ||
        (tags.command != nullptr && tags.command != call.command_buffer)) {
        tags.rejected = true;
        return;
    }
    tags.token = call.token;
    tags.command = call.command_buffer;
    tags.last_activity_serial = activity_serial;
    tags.bindings[index] = *binding;
    tags.mask |= static_cast<std::uint8_t>(1u << index);
}

struct CpuResourceReceipt {
    d::Identity identity{};
    std::uint32_t viewport{UINT32_MAX};
    const void* token{};
    void* command{};
    std::array<Binding, 4> bindings{};
    bool valid{};
    // API acceptance is not resource ownership, command-list generation or
    // GPU completion. No consumer may promote this receipt to those proofs.
    static constexpr bool resource_ownership_verified = false;
    static constexpr bool gpu_completion_verified = false;
};

inline CpuResourceReceipt evaluate(CpuTags& tags, const d::Scope& scope,
    const d::Identity& identity, const d::Viewport& viewport, const d::EvaluateCall& call,
    std::uintptr_t return_rva, int sdk_result, std::uint64_t activity_serial) {
    CpuResourceReceipt receipt{};
    bool accepted = tags.active && !tags.rejected && tags.mask == 0xF &&
        same_scope(tags.scope, scope) && d::scoped_sdk_call(scope, d::Stage::evaluation, identity, viewport) &&
        call.feature == d::dlss && call.count == 1 && call.inputs != nullptr &&
        d::known_evaluate_return(call.feature, return_rva) && sdk_result == 0 &&
        activity_serial == tags.last_activity_serial + 1 &&
        call.token != nullptr && call.token == tags.token &&
        call.command_buffer != nullptr && call.command_buffer == tags.command;
    if (accepted) {
        const auto& b = tags.bindings;
        accepted = b[0].tag.extent == b[1].tag.extent && b[0].tag.extent == b[2].tag.extent &&
            b[1].resource.state == 0 && b[3].resource.state == 8 &&
            b[3].tag.extent.width >= b[2].tag.extent.width &&
            b[3].tag.extent.height >= b[2].tag.extent.height;
        for (std::size_t i = 0; i < b.size(); ++i)
            for (std::size_t j = i + 1; j < b.size(); ++j)
                accepted = accepted && b[i].resource.native != b[j].resource.native;
    }
    if (accepted) {
        receipt.identity = identity;
        receipt.viewport = viewport.id;
        receipt.token = tags.token;
        receipt.command = tags.command;
        receipt.bindings = tags.bindings;
        receipt.valid = true;
    }
    // Consume even a failed evaluation; no stale or second evaluation proof.
    tags = {};
    return receipt;
}

} // namespace w3vr::engine_dlss_resources
