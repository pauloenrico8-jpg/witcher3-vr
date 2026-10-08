#include "engine_camera_temporal.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace camera = w3vr::engine_camera;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

enum class BuilderResult { valid, wrong_pointer, null_pointer, invalid_record };
BuilderResult builder_result{};
int builder_calls{};
camera::TemporalInputs observed{};

void* __fastcall fake_builder(void* destination, float time,
    const void* position, const void* rotation, float fov, float aspect,
    float near_plane, float far_plane, float projection_scale) {
    ++builder_calls;
    require(reinterpret_cast<std::uintptr_t>(destination) % 16 == 0,
        "Native temporary record is not aligned");
    require(reinterpret_cast<std::uintptr_t>(position) % 16 == 0 &&
        reinterpret_cast<std::uintptr_t>(rotation) % 16 == 0,
        "Native input vectors are not aligned");
    std::memcpy(observed.position.data(), position, sizeof(observed.position));
    std::memcpy(observed.rotation.data(), rotation, sizeof(observed.rotation));
    observed.time = time;
    observed.fov = fov;
    observed.aspect = aspect;
    observed.near_plane = near_plane;
    observed.far_plane = far_plane;
    observed.projection_scale = projection_scale;
    std::memset(destination, 0xC3, camera::record_bytes);
    if (builder_result == BuilderResult::null_pointer) return nullptr;
    if (builder_result == BuilderResult::wrong_pointer) return &observed;
    if (builder_result == BuilderResult::invalid_record) {
        static_cast<std::uint8_t*>(destination)[0] = 0;
    }
    return destination;
}

std::array<float, 14> source_view{12.0f, -35.0f, 6.25f, 1.0f,
    3.0f, 14.0f, -28.0f, 93.0f, 99.0f, 101.0f,
    1.2f, 0.75f, 0.08f, 1000.0f};

std::vector<std::uint8_t> prefix(const std::array<float, 14>& values) {
    std::vector<std::uint8_t> bytes(camera::source_prefix_bytes + 1);
    std::memcpy(bytes.data() + 1, values.data(), camera::source_prefix_bytes);
    return bytes;
}

void test_native_argument_contract() {
    const auto bytes = prefix(source_view);
    camera::TemporalRecord record{};
    const auto input = std::span<const std::uint8_t>(bytes).subspan(1);
    require(camera::build_record(input, 41.25f, fake_builder, record),
        "Valid, unaligned source rejected");
    require(builder_calls == 1, "Unexpected native call count");
    require(observed.position == std::array<float, 4>{12.0f, -35.0f, 6.25f, 1.0f},
        "Position vector was truncated or reordered");
    require(observed.rotation == std::array<float, 4>{3.0f, 14.0f, -28.0f, 93.0f},
        "Rotation vector was truncated or reordered");
    require(observed.time == 41.25f && observed.fov == 93.0f &&
        observed.aspect == 1.2f && observed.near_plane == 0.08f &&
        observed.far_plane == 1000.0f && observed.projection_scale == 0.75f,
        "Native scalar arguments were swapped");
    require(std::all_of(record.begin(), record.end(),
        [](std::uint8_t value) { return value == 0xC3; }),
        "Native result was not committed completely");
}

void test_rejected_sources_do_not_call_native() {
    const int initial_calls = builder_calls;
    camera::TemporalRecord record;
    record.fill(0x6D);
    const auto original = record;
    const auto bytes = prefix(source_view);
    const auto input = std::span<const std::uint8_t>(bytes).subspan(1);
    for (std::size_t length = 0; length < camera::source_prefix_bytes; ++length) {
        require(!camera::build_record(input.first(length), 1.0f,
            fake_builder, record), "Truncated source accepted");
    }
    for (std::size_t field : {0u, 1u, 2u, 4u, 5u, 6u, 7u,
            10u, 11u, 12u, 13u}) {
        for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity()}) {
            auto altered = source_view;
            altered[field] = bad;
            const auto invalid = prefix(altered);
            require(!camera::build_record(
                std::span<const std::uint8_t>(invalid).subspan(1), 1.0f,
                fake_builder, record), "Nonfinite input reached native code");
        }
    }
    for (auto [field, value] : std::array<std::pair<std::size_t, float>, 6>{
            {{7, 0.1f}, {10, 0.1f}, {12, 0.0f}, {12, -1.0f},
                {13, 0.08f}, {13, 0.01f}}}) {
        auto altered = source_view;
        altered[field] = value;
        const auto invalid = prefix(altered);
        require(!camera::build_record(
            std::span<const std::uint8_t>(invalid).subspan(1), 1.0f,
            fake_builder, record), "Degenerate projection reached native code");
    }
    require(!camera::build_record(input,
        std::numeric_limits<float>::quiet_NaN(), fake_builder, record),
        "Nonfinite time accepted");
    require(!camera::build_record(input, 1.0f, nullptr, record),
        "Missing native function accepted");
    require(builder_calls == initial_calls, "Rejected source called native code");
    require(record == original, "Rejected source changed output history");
}

void test_failed_native_result_does_not_publish() {
    const auto bytes = prefix(source_view);
    camera::TemporalRecord record;
    record.fill(0x6D);
    const auto original = record;
    for (auto failure : {BuilderResult::wrong_pointer,
            BuilderResult::null_pointer, BuilderResult::invalid_record}) {
        builder_result = failure;
        require(!camera::build_record(
            std::span<const std::uint8_t>(bytes).subspan(1), 1.0f,
            fake_builder, record), "Failed native result accepted");
        require(record == original, "Failed constructor published partial data");
    }
    builder_result = BuilderResult::valid;
}

void test_uninterpreted_source_words_are_preserved() {
    auto altered = source_view;
    altered[3] = std::numeric_limits<float>::quiet_NaN();
    altered[8] = std::numeric_limits<float>::quiet_NaN();
    altered[9] = std::numeric_limits<float>::infinity();
    const auto bytes = prefix(altered);
    const auto original = bytes;
    camera::TemporalRecord record{};
    require(camera::build_record(
        std::span<const std::uint8_t>(bytes).subspan(1), 2.0f,
        fake_builder, record), "Uninterpreted words were treated as projection inputs");
    require(std::memcmp(&observed.position[3], &altered[3], sizeof(float)) == 0,
        "Fourth position word was changed");
    require(bytes == original, "Native construction modified the source view");
}

void test_version_specific_history_boundaries() {
    camera::TemporalRecord record;
    for (std::size_t index = 0; index < record.size(); ++index) {
        record[index] = static_cast<std::uint8_t>((index % 254) + 1);
    }
    for (const auto& contract : {camera::legacy_404, camera::remastered_500c, camera::remastered_1048522}) {
        const auto end = contract.previous_record_offset + camera::record_bytes;
        std::vector<std::uint8_t> view(end + 32, 0xA7);
        require(camera::write_previous_record(view, contract, record),
            "Complete camera range rejected");
        for (std::size_t index = 0; index < view.size(); ++index) {
            const bool in_record = index >= contract.previous_record_offset && index < end;
            const auto relative = in_record ? index - contract.previous_record_offset : 0;
            const bool native_field = relative == 0 || (relative >= 4 && relative < 12) ||
                (relative >= 16 && relative < 44) || (relative >= 48 && relative < 176);
            const bool copied = in_record &&
                (contract.record_copy == camera::RecordCopy::whole_record || native_field);
            const auto expected = copied ? record[relative] : 0xA7;
            require(view[index] == expected,
                "History write changed a byte outside its version's record");
        }
        std::fill(view.begin(), view.end(), 0xA7);
        const auto original = view;
        require(!camera::write_previous_record(
            std::span<std::uint8_t>(view).first(end - 1), contract, record),
            "Truncated destination accepted");
        require(view == original, "Truncated destination was partly written");
        require(camera::write_previous_record(
            std::span<std::uint8_t>(view).first(end), contract, record),
            "Exact destination boundary rejected");
    }
    std::vector<std::uint8_t> remastered(0x5E0, 0xA7);
    require(camera::write_previous_record(remastered, camera::remastered_500c, record),
        "Remastered history rejected");
    require(std::all_of(remastered.begin() + 0x460, remastered.begin() + 0x510,
        [](std::uint8_t value) { return value == 0xA7; }),
        "Remastered history still used the legacy record offset");
    const auto original = remastered;
    auto invalid_contract = camera::remastered_500c;
    invalid_contract.previous_record_offset = std::numeric_limits<std::size_t>::max();
    require(!camera::write_previous_record(remastered, invalid_contract, record),
        "Overflowing offset accepted");
    invalid_contract.previous_record_offset = 0;
    require(!camera::write_previous_record(remastered, invalid_contract, record),
        "Record overlaps camera inputs");
    invalid_contract = camera::remastered_500c;
    invalid_contract.record_copy = static_cast<camera::RecordCopy>(255);
    require(!camera::write_previous_record(remastered, invalid_contract, record),
        "Unknown copy policy accepted");
    require(remastered == original, "Rejected history write modified the camera");
    record[0] = 0;
    std::vector<std::uint8_t> legacy(0x510, 0xA7);
    const auto legacy_original = legacy;
    require(!camera::write_previous_record(legacy, camera::legacy_404, record),
        "Legacy invalid-history admission changed");
    require(legacy == legacy_original, "Rejected legacy history modified the camera");
}

void test_remastered_reset_preserves_native_padding() {
    for (const auto& contract : {camera::remastered_500c, camera::remastered_1048522}) {
        camera::TemporalRecord reset;
        for (std::size_t i = 0; i < reset.size(); ++i) reset[i] = std::uint8_t(i + 1);
        reset[0] = 0; // Explicit, complete reset input, not a failed builder output.
        std::vector<std::uint8_t> view(0x600, 0xD7);
        require(camera::write_previous_record(view, contract, reset),
            "Explicit Remastered camera reset rejected");
        require(view[0x530] == 0, "Previous-camera validity was not reset");
        for (std::size_t i = 0; i < view.size(); ++i) {
            const bool in_record = i >= 0x530 && i < 0x5E0;
            const auto r = in_record ? i - 0x530 : 0;
            const bool field = r == 0 || (r >= 4 && r < 12) ||
                (r >= 16 && r < 44) || (r >= 48 && r < 176);
            require(view[i] == (in_record && field ? reset[r] : 0xD7),
                "Reset omitted a native field or changed padding/another camera field");
        }
        const auto original = view;
        require(!camera::write_previous_record(std::span<std::uint8_t>(view).first(0x5DF),
            contract, reset), "Truncated reset destination accepted");
        require(view == original, "Rejected reset was partly written");
    }
}

void test_overlapping_record_source_is_staged() {
    camera::TemporalRecord input;
    for (std::size_t i = 0; i < input.size(); ++i) input[i] = std::uint8_t(i + 1);
    for (const auto& contract : {camera::legacy_404, camera::remastered_500c, camera::remastered_1048522}) {
        for (const int displacement : {-64, -16, -1, 0, 1, 16, 64}) {
            const auto destination = contract.previous_record_offset;
            const auto source = std::size_t(std::ptrdiff_t(destination) + displacement);
            std::vector<std::uint8_t> bytes(destination + camera::record_bytes + 96, 0xE9);
            auto* alias = std::construct_at(
                reinterpret_cast<camera::TemporalRecord*>(bytes.data() + source), input);
            const auto before = bytes;
            require(camera::write_previous_record(bytes, contract, *alias),
                "Overlapping record source rejected");
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                const bool in_record = i >= destination && i < destination + camera::record_bytes;
                const auto r = in_record ? i - destination : 0;
                const bool field = r == 0 || (r >= 4 && r < 12) ||
                    (r >= 16 && r < 44) || (r >= 48 && r < 176);
                const bool copied = in_record &&
                    (contract.record_copy == camera::RecordCopy::whole_record || field);
                require(bytes[i] == (copied ? input[r] : before[i]),
                    "Overlap corrupted a later field, padding or unrelated byte");
            }
            std::destroy_at(alias);
        }
    }
}

int main() {
    test_native_argument_contract();
    test_rejected_sources_do_not_call_native();
    test_failed_native_result_does_not_publish();
    test_uninterpreted_source_words_are_preserved();
    test_version_specific_history_boundaries();
    test_remastered_reset_preserves_native_padding();
    test_overlapping_record_source_is_staged();
    std::cout << "Camera ABI argument order, staged failures and three version-specific "
        "history boundaries, Remastered field/reset copies and 21 overlapping copies "
        "passed (simulated native function only).\n";
}
