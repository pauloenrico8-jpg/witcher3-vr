#include "engine_camera_layout.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace layout = w3vr::engine_camera_layout;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

template<class T>
void put(std::vector<std::uint8_t>& bytes, std::size_t offset, const T& value) {
    require(offset <= bytes.size() && sizeof(value) <= bytes.size() - offset,
        "Fixture write exceeds its allocation");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void test_remastered_native_fixture() {
    // Deliberately use independent native evidence offsets, not offsets read
    // back from the implementation. Frame+0x10 contains the descriptor; its
    // cameras start at +0x10/+0x5F0. Prefix bytes differ between the two internal camera records.
    std::vector<std::uint8_t> frame(1 + 0xBEC + 16, 0xA7);
    const std::array<float, 2> matrix_trap{800.0f, -900.0f};
    for (std::size_t camera_index = 0; camera_index < 2; ++camera_index) {
        const auto view = 1 + (camera_index == 0 ? 0x20 : 0x600);
        std::array<float, 14> core{};
        core[0] = camera_index == 0 ? -0.032f : 0.032f;
        core[7] = 93.0f;
        core[10] = 1.2f;
        put(frame, view, core);
        put(frame, view + 0x400, matrix_trap);
        put(frame, view + 0x468, -77.0f); // obsolete history position
        put(frame, view + 0x4C0, std::array<float, 2>{0.25f, -0.375f});
        put(frame, view + 0x4C8, std::array<std::uint32_t, 2>{2064, 2208});
        put(frame, view + 0x538, 91.0f);
        layout::Fields fields{};
        std::size_t camera_offset{};
        require(layout::camera_offset(layout::remastered_500c,
            layout::Origin::frame, camera_index, camera_offset), "Missing frame camera");
        require(layout::read_fields(std::span<const std::uint8_t>(frame)
            .subspan(1 + camera_offset, 0x5E0), layout::remastered_500c, fields),
            "Complete unaligned Remastered camera rejected");
        require(fields.core == core && fields.alternate_fov == 91.0f &&
            fields.jitter == std::array<float, 2>{0.25f, -0.375f} &&
            fields.viewport == std::array<std::uint32_t, 2>{2064, 2208},
            "Camera read mixed matrix bytes, old fields, or the other camera");
        std::size_t descriptor_offset{};
        require(layout::camera_offset(layout::remastered_500c,
            layout::Origin::descriptor, camera_index, descriptor_offset) &&
            camera_offset == descriptor_offset + 0x10,
            "Descriptor and frame anchors were conflated");
    }
    require(layout::remastered_500c.frame_descriptor +
        layout::remastered_500c.descriptor_extent == 0xBEC,
        "Frame input extent was confused with camera viewport");
}

void test_legacy_fixture_and_failed_reads() {
    std::vector<std::uint8_t> camera(0x510, 0xA7);
    put(camera, 0x400, std::array<float, 2>{-0.125f, 0.5f});
    put(camera, 0x408, std::array<std::uint32_t, 2>{1920, 1080});
    put(camera, 0x468, 87.0f);
    layout::Fields fields{};
    require(layout::read_fields(camera, layout::legacy_404, fields),
        "Legacy camera behavior regressed");
    require(fields.alternate_fov == 87.0f &&
        fields.jitter == std::array<float, 2>{-0.125f, 0.5f} &&
        fields.viewport == std::array<std::uint32_t, 2>{1920, 1080},
        "Legacy field placement changed");
    const auto before = fields;
    const auto source_before = camera;
    for (std::size_t length = 0; length < camera.size(); ++length) {
        require(!layout::read_fields(std::span<const std::uint8_t>(camera)
            .first(length), layout::legacy_404, fields),
            "Truncated camera accepted");
        require(std::memcmp(&fields, &before, sizeof(fields)) == 0,
            "Failed read published a partial camera snapshot");
    }
    auto invalid = layout::legacy_404;
    invalid.viewport = std::numeric_limits<std::size_t>::max();
    require(!layout::read_fields(camera, invalid, fields) &&
        std::memcmp(&fields, &before, sizeof(fields)) == 0,
        "Overflowing field offset accepted or changed the snapshot");
    require(camera == source_before, "Snapshot reader changed native bytes");
    const std::vector<std::uint8_t> new_camera(0x5E0, 0xA7);
    for (std::size_t length = 0; length < new_camera.size(); ++length) {
        require(!layout::read_fields(std::span<const std::uint8_t>(new_camera)
            .first(length), layout::remastered_500c, fields) &&
            std::memcmp(&fields, &before, sizeof(fields)) == 0,
            "Truncated Remastered read published incomplete fields");
    }
    std::size_t result = 0xCAFE;
    require(!layout::camera_offset(layout::legacy_404,
        layout::Origin::frame, 2, result) && result == 0xCAFE,
        "Invalid camera index accepted or changed the output");
    require(!layout::camera_offset(layout::legacy_404,
        static_cast<layout::Origin>(99), 0, result) && result == 0xCAFE,
        "Unknown pointer origin defaulted to a descriptor");
    require(layout::camera_offset(layout::legacy_404,
        layout::Origin::frame, 1, result) && result == 0x530,
        "Legacy secondary frame camera changed");
}

void test_selection_and_writer_route_gate() {
    using namespace w3vr::engine_camera;
    const auto copied = remastered_500c;
    require(layout::selected(nullptr) == nullptr &&
        layout::selected(&copied) == nullptr &&
        layout::selected(&legacy_404) == &layout::legacy_404 &&
        layout::selected(&remastered_500c) == &layout::remastered_500c,
        "Unknown contract defaulted to an authorized camera layout");
    require(layout::legacy_temporal_writer_allowed(&legacy_404) &&
        !layout::legacy_temporal_writer_allowed(nullptr) &&
        !layout::legacy_temporal_writer_allowed(&remastered_500c) &&
        !layout::legacy_temporal_writer_allowed(&copied),
        "Camera layout evidence enabled legacy temporal hooks for Remastered");
    require(layout::remastered_writer_purpose(0x01D59287) ==
        layout::WriterPurpose::supersample_apply &&
        layout::remastered_writer_purpose(0x01D59880) ==
        layout::WriterPurpose::supersample_restore &&
        layout::remastered_writer_purpose(0x01D59D27) ==
        layout::WriterPurpose::final_2d_override,
        "New writer callers lost their separate purpose");
    for (const auto address : {0x01D59282, 0x01D5987B, 0x01D59D22,
            0x01D86EA5, 0x01D87EC0, 0}) {
        require(layout::remastered_writer_purpose(address) ==
            layout::WriterPurpose::unknown,
            "CALL address or legacy return treated as a new writer route");
    }
}

int main() {
    test_remastered_native_fixture();
    test_legacy_fixture_and_failed_reads();
    test_selection_and_writer_route_gate();
}
