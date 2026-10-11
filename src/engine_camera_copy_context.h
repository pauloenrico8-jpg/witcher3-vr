#pragma once

#include "engine_camera_temporal.h"

#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace w3vr::camera_copy {
// Synchronous native call lineage only. No pointer is dereferenced, owned,
// retained or made safe by this context. This is NOT permission to write a
// pose, proof of GPU output, or an object/thread/lifecycle contract.
inline constexpr std::uintptr_t descriptor_copy_rva = 0x00324430;
inline constexpr std::uintptr_t scratch_return_rva = 0x01B8067B;
inline constexpr std::uintptr_t frame_return_rva = 0x01B80709;
inline constexpr std::array<std::uintptr_t, 2> camera_returns{0x00324460, 0x00324473};
inline constexpr std::array<std::uintptr_t, 2> camera_offsets{0x10, 0x5F0};
inline constexpr std::uintptr_t rebuild_return_rva = 0x0228AA4F;
inline constexpr std::uintptr_t descriptor_bytes = 0xF750;
inline constexpr std::uintptr_t frame_descriptor_offset = 0x10;
inline constexpr std::array<std::uint8_t, 15> descriptor_signature{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
inline constexpr std::array<std::uint8_t, 16> camera_signature{
    0x40,0x53,0x48,0x83,0xEC,0x30,0x8B,0x02,0x48,0x8B,0xD9,0x89,0x01,0x8B,0x42,0x04};
inline constexpr std::array<std::uint8_t, 14> rebuild_signature{
    0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x48,0x8D,0xA8,0x08,0xFB,0xFF,0xFF};
// Independently checked complete-instruction prefixes for the current build.
inline constexpr std::array<std::uint8_t, 15> current_descriptor_signature{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
inline constexpr std::array<std::uint8_t, 16> current_camera_signature{
    0x40,0x53,0x48,0x83,0xEC,0x30,0x8B,0x02,0x48,0x8B,0xD9,0x89,0x01,0x8B,0x42,0x04};
inline constexpr std::array<std::uint8_t, 14> current_rebuild_signature{
    0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x48,0x8D,0xA8,0x08,0xFB,0xFF,0xFF};
enum class HookKind : std::size_t { descriptor, camera, rebuild };
struct HookTarget { std::uintptr_t rva; std::span<const std::uint8_t> signature; };
struct Profile {
    std::uintptr_t descriptor_copy, scratch_return, frame_return;
    std::array<std::uintptr_t, 2> camera_returns;
    std::uintptr_t rebuild_return;
    std::array<HookTarget, 3> hooks;
};
inline constexpr Profile remastered_500c{descriptor_copy_rva,
    scratch_return_rva, frame_return_rva, camera_returns, rebuild_return_rva,
    {{{descriptor_copy_rva, descriptor_signature}, {0x0228A970, camera_signature},
      {0x0228AB40, rebuild_signature}}}};
inline constexpr Profile remastered_1048522{0x00322F40, 0x01B86F3B, 0x01B86FC9,
    {0x00322F70, 0x00322F83}, 0x0229314F,
    {{{0x00322F40, current_descriptor_signature}, {0x02293070, current_camera_signature},
      {0x02293240, current_rebuild_signature}}}};
inline bool known(const Profile* profile) {
    return profile == &remastered_500c || profile == &remastered_1048522;
}
inline const Profile* selected(const engine_camera::TemporalContract* contract) {
    if (contract == &engine_camera::remastered_500c) return &remastered_500c;
    if (contract == &engine_camera::remastered_1048522) return &remastered_1048522;
    return nullptr;
}
inline bool ready_for(const engine_camera::TemporalContract* contract,
    const Profile* installed_profile, bool observers_ready) {
    return observers_ready && known(installed_profile) && selected(contract) == installed_profile;
}
inline const HookTarget* hook_target(const Profile* profile, HookKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    return known(profile) && index < profile->hooks.size() ? &profile->hooks[index] : nullptr;
}

// Caller checks all three Native prefixes and admits one installation attempt.
// This helper neither publishes readiness nor removes/nulls trampolines. The
// real host supplies MinHook; CPU tests supply failure spies, not Native calls.
template<class Create, class Enable, class Disable, class Current>
bool install_observers(const engine_camera::TemporalContract* contract,
    const Profile* profile, Create create, Enable enable, Disable disable, Current current) {
    if (!known(profile) || selected(contract) != profile) return false;
    std::size_t created{}, enabled{};
    for (std::size_t i=0; i<profile->hooks.size(); ++i) {
        if (!current() || !create(i)) break;
        ++created;
    }
    if (created == profile->hooks.size()) for (std::size_t i=0; i<profile->hooks.size(); ++i) {
        if (!current() || !enable(i)) break;
        ++enabled;
    }
    if (created == profile->hooks.size() && enabled == profile->hooks.size() && current()) return true;
    for (std::size_t i=0; i<enabled; ++i) disable(i);
    return false; // Disabled entry points are NOT an external all-call/unload barrier.
}
inline bool signature_matches(std::span<const std::uint8_t> expected,
    std::span<const std::uint8_t> bytes) {
    if (expected.empty() || bytes.size() < expected.size()) return false;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if (expected[i] != bytes[i]) return false;
    return true;
}
inline bool range(std::uintptr_t base) {
    return base != 0 && base <= std::numeric_limits<std::uintptr_t>::max() - descriptor_bytes;
}
inline bool separate(std::uintptr_t a, std::uintptr_t b) {
    return range(a) && range(b) &&
        (a + descriptor_bytes <= b || b + descriptor_bytes <= a);
}

enum class Stage { unknown, scratch, frame };
enum class Phase { awaiting_scratch, copying_scratch, scratch_ready, copying_frame, complete, broken };
struct FactoryScope;
struct DescriptorScope;
struct CameraScope;
struct Context {
    FactoryScope* factory{};
    DescriptorScope* descriptor{};
    CameraScope* camera{};
};
struct Route {
    Stage stage{Stage::unknown};
    int camera_index{-1}; // TWO records within ONE scene, NOT the two HMD eyes.
    int eye{-1};         // Independent factory invocation's externally provided eye.
    std::uint64_t pair{};
    const Profile* profile{}; // Borrowed static profile, not native ownership.
};

// Call scopes always mask outer scopes, including when admission fails.
// Nesting is local to the calling thread; no cross-thread authority is implied.
struct FactoryScope {
    Context& context;
    const Context previous;
    const std::uintptr_t input;
    const std::uint64_t pair;
    const int eye;
    const Profile* const profile;
    bool admitted;
    bool finished{};
    Phase phase{Phase::awaiting_scratch};
    std::uintptr_t scratch{}, frame_descriptor{};
    FactoryScope(Context& c, const engine_camera::TemporalContract* contract,
        std::uintptr_t descriptor, std::uint64_t pair_id, int eye_id,
        bool normal_producer, const Profile* installed_profile, bool observers_ready)
        : context(c), previous(c), input(descriptor), pair(pair_id), eye(eye_id),
          profile(selected(contract)), admitted(profile != nullptr &&
            normal_producer && ready_for(contract, installed_profile, observers_ready) && range(input) &&
            pair != 0 && pair != UINT64_MAX && eye >= 0 && eye <= 1) {
        context = {this, nullptr, nullptr};
    }
    ~FactoryScope() { context = previous; }
    FactoryScope(const FactoryScope&) = delete;
    FactoryScope& operator=(const FactoryScope&) = delete;
    bool finish(std::uintptr_t native_frame, const Profile* installed_profile, bool observers_ready) {
        const bool matched = !finished && admitted && observers_ready && known(installed_profile) &&
            profile == installed_profile && phase == Phase::complete && native_frame != 0 &&
            native_frame <= std::numeric_limits<std::uintptr_t>::max() - frame_descriptor_offset &&
            native_frame + frame_descriptor_offset == frame_descriptor;
        finished = true;
        if (!matched) phase = Phase::broken;
        return matched;
    }
};

// Every observer also checks the installed profile. A rejected callback masks
// the previous scope while Native runs, then restores it on return/unwinding.
struct ObserverScope {
    Context& context;
    const Context previous;
    const bool admitted;
    ObserverScope(Context& c, const engine_camera::TemporalContract* contract,
        const Profile* installed_profile, bool observers_ready)
        : context(c), previous(c), admitted(ready_for(contract, installed_profile, observers_ready) &&
            (!c.factory || (c.factory->admitted && c.factory->profile == installed_profile))) {
        if (!admitted) context = {};
    }
    ~ObserverScope() { context = previous; }
    ObserverScope(const ObserverScope&) = delete;
    ObserverScope& operator=(const ObserverScope&) = delete;
};

struct DescriptorScope {
    Context& context;
    const Context previous;
    FactoryScope* const factory;
    const std::uintptr_t destination, source;
    Stage stage{Stage::unknown};
    unsigned camera_mask{};
    bool finished{};
    DescriptorScope(Context& c, std::uintptr_t caller, std::uintptr_t dst, std::uintptr_t src)
        : context(c), previous(c), factory(c.factory), destination(dst), source(src) {
        context.descriptor = this; context.camera = nullptr;
        if (!factory || !factory->admitted) return;
        const auto& p = *factory->profile;
        if (!separate(dst, src)) {
            if (caller == p.scratch_return || caller == p.frame_return)
                factory->phase = Phase::broken;
            return;
        }
        if (caller == p.scratch_return && factory->phase == Phase::awaiting_scratch &&
            src == factory->input) {
            stage = Stage::scratch; factory->phase = Phase::copying_scratch;
        } else if (caller == p.frame_return && factory->phase == Phase::scratch_ready &&
            src == factory->scratch && separate(dst, factory->input)) {
            stage = Stage::frame; factory->phase = Phase::copying_frame;
        } else if (caller == p.scratch_return || caller == p.frame_return) {
            factory->phase = Phase::broken;
        }
    }
    ~DescriptorScope() {
        if (stage != Stage::unknown && !finished) factory->phase = Phase::broken;
        context = previous;
    }
    DescriptorScope(const DescriptorScope&) = delete;
    DescriptorScope& operator=(const DescriptorScope&) = delete;
    bool finish(std::uintptr_t native_result) {
        if (finished || !factory || stage == Stage::unknown) return false;
        finished = true;
        const auto expected = stage == Stage::scratch ? Phase::copying_scratch : Phase::copying_frame;
        if (context.factory != factory || context.descriptor != this ||
            native_result != destination || camera_mask != 3 || factory->phase != expected) {
            factory->phase = Phase::broken; return false;
        }
        if (stage == Stage::scratch) {
            factory->scratch = destination; factory->phase = Phase::scratch_ready;
        } else {
            factory->frame_descriptor = destination; factory->phase = Phase::complete;
        }
        return true;
    }
};

struct CameraScope {
    Context& context;
    const Context previous;
    DescriptorScope* const descriptor;
    const std::uintptr_t destination;
    int index{-1};
    bool observed{}, finished{};
    CameraScope(Context& c, std::uintptr_t caller, std::uintptr_t dst, std::uintptr_t src)
        : context(c), previous(c), descriptor(c.descriptor), destination(dst) {
        context.camera = this;
        if (!descriptor || descriptor->stage == Stage::unknown || descriptor->finished ||
            context.factory != descriptor->factory || !descriptor->factory->admitted) return;
        const auto expected = descriptor->stage == Stage::scratch ? Phase::copying_scratch : Phase::copying_frame;
        if (descriptor->factory->phase != expected) return;
        const auto& p = *descriptor->factory->profile;
        for (int i = 0; i < 2; ++i) {
            if (caller == p.camera_returns[i] && dst == descriptor->destination + camera_offsets[i] &&
                src == descriptor->source + camera_offsets[i] &&
                descriptor->camera_mask == (i == 0 ? 0u : 1u)) { index = i; return; }
        }
        if (caller == p.camera_returns[0] || caller == p.camera_returns[1])
            descriptor->factory->phase = Phase::broken;
    }
    ~CameraScope() {
        if (index >= 0 && !finished) descriptor->factory->phase = Phase::broken;
        context = previous;
    }
    CameraScope(const CameraScope&) = delete;
    CameraScope& operator=(const CameraScope&) = delete;
    bool finish(std::uintptr_t native_result) {
        if (index < 0 || finished) return false;
        finished = true;
        const auto expected = descriptor->stage == Stage::scratch ? Phase::copying_scratch : Phase::copying_frame;
        if (context.factory != descriptor->factory || context.descriptor != descriptor ||
            context.camera != this || !observed || native_result != destination ||
            descriptor->factory->phase != expected) {
            descriptor->factory->phase = Phase::broken; return false;
        }
        descriptor->camera_mask |= 1u << index;
        return true;
    }
};

inline Route observe_rebuild(Context& context, std::uintptr_t caller, std::uintptr_t view) {
    auto* camera = context.camera;
    auto* descriptor = context.descriptor;
    auto* factory = context.factory;
    if (!factory || !descriptor || !camera || !factory->admitted ||
        camera->descriptor != descriptor || descriptor->factory != factory ||
        camera->index < 0 || camera->finished || descriptor->finished ||
        caller != factory->profile->rebuild_return) return {};
    if (view != camera->destination) { factory->phase = Phase::broken; return {}; }
    const auto expected = descriptor->stage == Stage::scratch ? Phase::copying_scratch : Phase::copying_frame;
    if (factory->phase != expected) return {};
    if (camera->observed) { factory->phase = Phase::broken; return {}; }
    camera->observed = true;
    return {descriptor->stage, camera->index, factory->eye, factory->pair, factory->profile};
}
} // namespace w3vr::camera_copy
