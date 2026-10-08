#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace w3vr::engine_camera {

// A contract for this camera subsystem only, not an executable compatibility
// check. The 5.00c entries were examined statically for executable SHA-256
// 9406ECCC12B68E08920931442EF6A57340E910D3E01F2082E88232487433FE51.
// They must not enable the remaining legacy engine/rendering hooks.
enum class RecordCopy { whole_record, remastered_fields };

struct TemporalContract {
    std::size_t previous_record_offset;
    std::uint32_t builder_rva;
    std::uint32_t rebuild_rva;
    std::uint32_t copy_rebuild_rva;
    RecordCopy record_copy{RecordCopy::whole_record};
};

inline constexpr TemporalContract legacy_404{0x460, 0x015FE010,
    0x015FE550, 0x015FF640};
inline constexpr TemporalContract remastered_500c{0x530, 0x02288F00,
    0x0228AB40, 0x0228A970, RecordCopy::remastered_fields};
// Only this camera subsystem was compared for build 1048522. Other selected()
// helpers still reject this contract until independently ported. No host or
// global executable admission is added by declaring the contract.
inline constexpr TemporalContract remastered_1048522{0x530, 0x02291600,
    0x02293240, 0x02293070, RecordCopy::remastered_fields};
inline constexpr std::size_t source_prefix_bytes = 0x38;
inline constexpr std::size_t record_bytes = 0xB0;
using TemporalRecord = std::array<std::uint8_t, record_bytes>;

#if defined(_MSC_VER)
using NativeBuilder = void*(__fastcall*)(void*, float, const void*,
    const void*, float, float, float, float, float);
#else
using NativeBuilder = void*(*)(void*, float, const void*,
    const void*, float, float, float, float, float);
#endif

// Both examined versions use XYZ at +0, Euler/FOV at +0x10, FOV +0x1C,
// aspect +0x28, projection scale +0x2C, near +0x30 and far +0x34.
// Keep all four position/rotation floats: native helpers can read a whole
// vector even though orientation uses only the first three components. Preserve
// the position's fourth word without assigning it an unproved float meaning.
struct alignas(16) TemporalInputs {
    std::array<float, 4> position;
    std::array<float, 4> rotation;
    float time;
    float fov;
    float aspect;
    float near_plane;
    float far_plane;
    float projection_scale;
};

inline bool read_temporal_inputs(std::span<const std::uint8_t> view,
    float time, TemporalInputs& result) {
    if (view.size() < source_prefix_bytes || !std::isfinite(time)) {
        return false;
    }
    std::array<float, source_prefix_bytes / sizeof(float)> source{};
    std::memcpy(source.data(), view.data(), source_prefix_bytes);
    for (std::size_t index : {0u, 1u, 2u, 4u, 5u, 6u, 7u}) {
        if (!std::isfinite(source[index])) return false;
    }
    if (source[7] <= 0.1f || !std::isfinite(source[10]) ||
        source[10] <= 0.1f || !std::isfinite(source[11]) ||
        !std::isfinite(source[12]) || source[12] <= 0.0f ||
        !std::isfinite(source[13]) || source[13] <= source[12]) {
        return false;
    }
    TemporalInputs candidate{{source[0], source[1], source[2], source[3]},
        {source[4], source[5], source[6], source[7]}, time, source[7],
        source[10], source[12], source[13], source[11]};
    result = candidate;
    return true;
}

// Stage the native result so a failed constructor cannot publish a half-built
// record to the per-eye history or to the game's camera. No native call is made
// for a malformed/truncated source. This does not catch native ABI faults.
inline bool build_record(std::span<const std::uint8_t> view, float time,
    NativeBuilder builder, TemporalRecord& result) {
    TemporalInputs inputs{};
    if (builder == nullptr || !read_temporal_inputs(view, time, inputs)) {
        return false;
    }
    alignas(16) TemporalRecord candidate{};
    void* built = builder(candidate.data(), inputs.time,
        inputs.position.data(), inputs.rotation.data(), inputs.fov,
        inputs.aspect, inputs.near_plane, inputs.far_plane,
        inputs.projection_scale);
    if (built != candidate.data() || candidate[0] == 0) return false;
    result = candidate;
    return true;
}

// The caller supplies a valid camera range owned by its native hook. A span
// checks offsets/lengths, not Windows page access or the identity of an object.
// Refuse an incomplete range before writing anything, and preserve every byte
// outside the version's defined previous-camera fields. Remastered also
// preserves native padding and accepts an explicit invalid/reset record.
inline bool write_previous_record(std::span<std::uint8_t> view,
    const TemporalContract& contract, const TemporalRecord& record) {
    const std::size_t offset = contract.previous_record_offset;
    const bool remastered_fields = contract.record_copy == RecordCopy::remastered_fields;
    if ((contract.record_copy != RecordCopy::whole_record && !remastered_fields) ||
        (!remastered_fields && record[0] == 0) || offset < source_prefix_bytes ||
        offset > view.size() || record_bytes > view.size() - offset) {
        return false;
    }
    if (remastered_fields) {
        // Native Remastered copies the defined fields, including a complete
        // invalid/reset record. Its padding belongs to the destination and
        // stays intact. The caller must reject a failed build_record result;
        // that function returns false and leaves its output intact.
        // Stage FIRST so overlapping ranges cannot overwrite a later source
        // field. This is a byte-copy contract, not history/ownership readiness.
        const TemporalRecord staged = record;
        struct FieldRange { std::size_t offset, bytes; };
        constexpr std::array<FieldRange, 4> fields{{
            {0x00, 0x01}, {0x04, 0x08}, {0x10, 0x1C}, {0x30, 0x80}}};
        for (const auto& field : fields) {
            std::memcpy(view.data() + offset + field.offset,
                staged.data() + field.offset, field.bytes);
        }
    } else {
        std::memmove(view.data() + offset, record.data(), record_bytes);
    }
    return true;
}

} // namespace w3vr::engine_camera
