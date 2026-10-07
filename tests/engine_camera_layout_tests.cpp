#include "engine_camera_layout.h"
#include "engine_camera_authority.h"
#include "engine_camera_copy_context.h"

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
    require(layout::remastered_writer_purpose(0x01C14BD1) ==
        layout::WriterPurpose::normal_temporal &&
        layout::remastered_writer_purpose(0x01D59287) ==
        layout::WriterPurpose::supersample_apply &&
        layout::remastered_writer_purpose(0x01D59880) ==
        layout::WriterPurpose::supersample_restore &&
        layout::remastered_writer_purpose(0x01D59D27) ==
        layout::WriterPurpose::final_2d_override,
        "New writer callers lost their separate purpose");
    for (const auto address : {0x01C14BCC, 0x01D59282, 0x01D5987B, 0x01D59D22,
            0x01D86EA5, 0x01D87EC0, 0}) {
        require(layout::remastered_writer_purpose(address) ==
            layout::WriterPurpose::unknown,
            "CALL address or legacy return treated as a new writer route");
    }
    require(layout::temporal_writer_rva(&legacy_404) == 0x015E5A90 &&
        layout::temporal_writer_rva(&remastered_500c) == 0x022B51E0 &&
        layout::temporal_writer_rva(nullptr) == 0 &&
        layout::temporal_writer_rva(&copied) == 0,
        "Unselected version received a native writer entry");
    for (const auto address : layout::legacy_writer_returns) {
        require(layout::normal_temporal_route(&legacy_404, address) &&
            !layout::normal_temporal_route(&remastered_500c, address),
            "Legacy return routes changed or authorized Remastered calls");
    }
    require(layout::normal_temporal_route(&remastered_500c, 0x01C14BD1) &&
        !layout::normal_temporal_route(&legacy_404, 0x01C14BD1),
        "The reached normal scene writer was not separated by version");
    for (const auto address : {0x01C14BCC, 0x01D59287, 0x01D59880, 0x01D59D27, 0}) {
        require(!layout::normal_temporal_route(&remastered_500c, address) &&
            !layout::normal_temporal_route(&copied, address) &&
            !layout::normal_temporal_route(nullptr, address),
            "Restore, 2D, CALL, or unknown version can apply another optical center");
    }
    require(layout::legacy_centered_writer_hint(&legacy_404, 0x01D87EC0) &&
        !layout::legacy_centered_writer_hint(&remastered_500c, 0x01D87EC0) &&
        !layout::legacy_centered_writer_hint(&remastered_500c, 0x01C14BD1),
        "Legacy already-centered hint leaked into Remastered");
    std::array<std::size_t, 2> jitter{0xCAFE, 0xBEEF};
    require(!layout::descriptor_jitter_offsets(nullptr, jitter) &&
        jitter == std::array<std::size_t, 2>{0xCAFE, 0xBEEF} &&
        !layout::descriptor_jitter_offsets(&copied, jitter),
        "Unknown contract changed offsets");
    require(layout::descriptor_jitter_offsets(&legacy_404, jitter) &&
        jitter == std::array<std::size_t, 2>{0x410, 0x920},
        "Legacy current jitter addresses changed");
    require(layout::descriptor_jitter_offsets(&remastered_500c, jitter) &&
        jitter == std::array<std::size_t, 2>{0x4D0, 0xAB0},
        "Current jitter confused with frame offsets or previous jitter +4E0");
}

void test_native_authority_version_and_unknowns() {
    namespace authority = w3vr::engine_camera_authority;
    namespace camera = w3vr::engine_camera;
    const auto copied = camera::remastered_500c;
    const auto* old = authority::selected(&camera::legacy_404);
    const auto* modern = authority::selected(&camera::remastered_500c);
    require(old && modern && authority::selected(nullptr) == nullptr &&
        authority::selected(&copied) == nullptr,
        "Unknown camera contract acquired callback entries");
    // Independent observations: callback registration and normal CameraDirector
    // group, not the second entity method with the same script name.
    constexpr std::array<std::uintptr_t, 9> entries{
        0x02102690, 0x02251A40, 0x02103880, 0x021038A0, 0x0226CDB0,
        0x01E07620, 0x0237A0A0, 0x02271A80, 0x0237A1C0};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto* entry = authority::entry(modern, static_cast<authority::Callback>(i));
        require(entry && entry->rva == entries[i] && !entry->signature.empty(),
            "Remastered callback entry missing or lacks examined bytes");
    }
    require(authority::entry(nullptr, authority::Callback::head) == nullptr &&
        authority::entry(modern, authority::Callback::count) == nullptr &&
        authority::entry(modern, static_cast<authority::Callback>(999)) == nullptr,
        "Invalid callback defaults to a native address");
    require(authority::entry(old, authority::Callback::head)->rva == 0x01C4B550 &&
        authority::entry(old, authority::Callback::bone_matrix)->rva == 0x0152E600 &&
        authority::entry(old, authority::Callback::direction)->rva == 0x0161BBC0,
        "Legacy native head or director routes changed");
    require(authority::entry(modern, authority::Callback::direction)->rva != 0x02470440,
        "An unrelated same-named script method owns the director");
    // These were separate fields in the native getters/setter, not a uniform
    // version delta. A trap at +259 simulates a nearby unrelated word.
    std::vector<std::uint8_t> object(0x270, 0);
    object[0x259] = 1;
    object[0x269] = 0;
    int value = -1;
    require(authority::read_flag(object, modern->manual_control, value) && value == 0,
        "Modern manual camera state came from obsolete +259");
    require(authority::read_flag(object, old->manual_control, value) && value == 1,
        "Legacy manual-control placement changed");
    object[0x269] = 0x80;
    require(authority::read_flag(object, modern->manual_control, value) && value == 1,
        "Nonzero native manual flag was not recognized");
    const auto unchanged = object;
    value = -1;
    require(!authority::read_flag(std::span<const std::uint8_t>(object).first(0x269),
        modern->manual_control, value) && value == -1 &&
        !authority::read_flag(object, std::numeric_limits<std::size_t>::max(), value) && value == -1,
        "Missing or overflowing authority field fabricated a camera state");
    require(object == unchanged && modern->actor_gameplay == 0x2F3 &&
        modern->actor_cutscene == 0x2F2 && modern->game_cutscene == 0x11F &&
        modern->director_entries == 0x58 && modern->director_count == 0x60 &&
        modern->entry_stride == 0x28 && modern->native_camera == 0,
        "Authority reads wrote data or shifted fields that did not move");
    require(authority::is_custom_camera(&camera::remastered_500c, 0x03805378) &&
        !authority::is_custom_camera(&camera::remastered_500c, 0x038058A0) &&
        !authority::is_custom_camera(&camera::remastered_500c, 0x038058D0) &&
        !authority::is_custom_camera(&camera::remastered_500c, 0x02D645B0) &&
        authority::is_custom_camera(&camera::legacy_404, 0x02D645B0) &&
        !authority::is_custom_camera(nullptr, 0) &&
        !authority::is_custom_camera(&copied, 0x03805378),
        "Secondary subobject, legacy type, or unknown profile became custom-camera authority");
}

void test_native_authority_entry_mismatch() {
    namespace authority = w3vr::engine_camera_authority;
    // Exact observed actor getter: script cursor +30, flag +2F3, byte output.
    const std::array<std::uint8_t, 20> getter{0x48,0xFF,0x42,0x30,0x4D,0x85,0xC0,
        0x74,0x0A,0x0F,0xB6,0x81,0xF3,0x02,0x00,0x00,0x41,0x88,0x00,0xC3};
    const auto& entry = authority::remastered_500c.entries[
        static_cast<std::size_t>(authority::Callback::actor_gameplay)];
    require(authority::signature_matches(entry, getter), "Examined actor getter rejected");
    for (std::size_t i = 0; i < getter.size(); ++i) {
        auto mutated = getter;
        mutated[i] ^= 0x80;
        require(!authority::signature_matches(entry, mutated),
            "Different native getter accepted");
        require(!authority::signature_matches(entry,
            std::span<const std::uint8_t>(getter).first(i)), "Truncated native entry accepted");
    }
    require(!authority::signature_matches(authority::Entry{0, getter}, getter) &&
        !authority::signature_matches(authority::Entry{entry.rva, {}}, getter),
        "Missing entry or evidence became a byte-verified callback");
}

void test_projection_writes_keep_integer_dimensions_and_other_records() {
    // Independent native byte offsets; these are two INTERNAL cameras of ONE
    // descriptor, not two headset images. Deliberately unalign the fixture.
    std::vector<std::uint8_t> descriptor(1 + 0xBDC + 16, 0x7A);
    auto expected = descriptor;
    const std::array<layout::ProjectionFields, 2> fields{{
        {{17.5f, -20.25f}, {2064, 2208}}, {{-33.5f, 19.125f}, {1832, 1920}}}};
    for (std::size_t i = 0; i < 2; ++i) {
        const std::size_t camera = 1 + (i == 0 ? 0x10 : 0x5F0);
        put(expected, camera + 0x4C0, fields[i].center_px);
        put(expected, camera + 0x4C8, fields[i].viewport);
        require(layout::write_projection(std::span<std::uint8_t>(descriptor)
            .subspan(camera, 0x5E0), &layout::remastered_500c, fields[i]),
            "Examined modern projection rejected");
        layout::ProjectionFields read{};
        require(layout::read_projection(std::span<const std::uint8_t>(descriptor)
            .subspan(camera, 0x5E0), &layout::remastered_500c, read) &&
            read.center_px == fields[i].center_px && read.viewport == fields[i].viewport,
            "Dimensions became floating-point bit patterns or camera fields were mixed");
    }
    require(descriptor == expected,
        "Projection write damaged old +400 matrix bytes, history, or neighbouring camera");
    std::vector<std::uint8_t> legacy(0x510 + 16, 0x3D);
    auto legacy_expected = legacy;
    put(legacy_expected, 0x400, fields[0].center_px);
    put(legacy_expected, 0x408, fields[0].viewport);
    require(layout::write_projection(legacy, &layout::legacy_404, fields[0]) &&
        legacy == legacy_expected, "Legacy projection placement or other fields changed");
}

void test_failed_projection_is_not_partially_published() {
    std::vector<std::uint8_t> camera(0x5E0, 0x7A);
    const auto unchanged = camera;
    const layout::ProjectionFields fields{{17.5f, -20.25f}, {2064, 2208}};
    const auto copied_layout = layout::remastered_500c;
    layout::ProjectionFields result{{-1, -2}, {13, 14}};
    const auto sentinel = result;
    for (std::size_t size = 0; size < camera.size(); ++size) {
        require(!layout::write_projection(std::span<std::uint8_t>(camera).first(size),
            &layout::remastered_500c, fields) && camera == unchanged,
            "Truncated camera received partial projection writes");
        require(!layout::read_projection(std::span<const std::uint8_t>(camera).first(size),
            &layout::remastered_500c, result) && result.center_px == sentinel.center_px &&
            result.viewport == sentinel.viewport, "Failed read published partial projection");
    }
    for (const auto* profile : {static_cast<const layout::Layout*>(nullptr), &copied_layout}) {
        require(!layout::write_projection(camera, profile, fields) && camera == unchanged &&
            !layout::read_projection(camera, profile, result), "Unknown layout wrote projection");
    }
    for (std::size_t i = 0; i < 2; ++i) {
        auto invalid = fields; invalid.viewport[i] = 0;
        require(!layout::write_projection(camera, &layout::remastered_500c, invalid) &&
            camera == unchanged, "Zero dimension partially changed camera");
        for (float bad : {std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()}) {
            invalid = fields; invalid.center_px[i] = bad;
            require(!layout::write_projection(camera, &layout::remastered_500c, invalid) &&
                camera == unchanged, "Nonfinite projection partially changed camera");
        }
    }
}

void test_canted_pose_commits_only_six_floats() {
    for (const auto* profile : {&layout::legacy_404, &layout::remastered_500c}) {
        std::vector<std::uint8_t> camera(1 + profile->camera_bytes + 64, 0x7A);
        const layout::PoseFields pose{{4, -8, 16}, {32, -64, 128}};
        auto expected = camera;
        put(expected, 1, pose.position); put(expected, 1 + 0x10, pose.rotation_degrees);
        auto target = std::span<std::uint8_t>(camera).subspan(1, profile->camera_bytes);
        require(layout::write_pose(target, profile, pose) && camera == expected,
            "Canted transform rewrote position W, FOV, matrices, projection, history or neighbours");
        const auto unchanged = camera;
        for (std::size_t size = 0; size < profile->camera_bytes; ++size) {
            require(!layout::write_pose(target.first(size), profile, pose) && camera == unchanged,
                "Incomplete camera received a pose");
        }
        for (std::size_t i = 0; i < 6; ++i) {
            auto invalid = pose;
            if (i < 3) invalid.position[i] = std::numeric_limits<float>::infinity();
            else invalid.rotation_degrees[i-3] = std::numeric_limits<float>::quiet_NaN();
            require(!layout::write_pose(target, profile, invalid) && camera == unchanged,
                "Nonfinite pose partially changed native fields");
        }
        const auto copied = *profile;
        require(!layout::write_pose(target, nullptr, pose) &&
            !layout::write_pose(target, &copied, pose) && camera == unchanged,
            "Unknown camera layout can commit canted pose");
    }
}

namespace copy = w3vr::camera_copy;
constexpr std::uintptr_t input_desc = 0x100000, scratch_desc = 0x200000, final_frame = 0x300000;

bool run_copy_camera(copy::Context& context, int index, std::uintptr_t destination,
    std::uintptr_t source, copy::Stage stage, int eye, std::uint64_t pair,
    bool omit_rebuild = false, bool wrong_return = false) {
    // Fabricated synchronous callbacks. No native game function, resource,
    // headset pose or render output is executed by these tests.
    copy::CameraScope camera(context, index == 0 ? 0x00324460 : 0x00324473,
        destination + (index == 0 ? 0x10 : 0x5F0),
        source + (index == 0 ? 0x10 : 0x5F0));
    if (!omit_rebuild) {
        const auto route = copy::observe_rebuild(context, 0x0228AA4F, camera.destination);
        require(route.stage == stage && route.camera_index == index &&
            route.eye == eye && route.pair == pair, "Rebuild mixed copy stage, internal record or factory eye");
    }
    return camera.finish(wrong_return ? camera.destination + 16 : camera.destination);
}

bool run_copy_descriptor(copy::Context& context, std::uintptr_t caller,
    std::uintptr_t destination, std::uintptr_t source, copy::Stage stage,
    int eye, std::uint64_t pair) {
    copy::DescriptorScope descriptor(context, caller, destination, source);
    require(run_copy_camera(context, 0, destination, source, stage, eye, pair) &&
        run_copy_camera(context, 1, destination, source, stage, eye, pair),
        "Complete camera copy rejected");
    return descriptor.finish(destination);
}

void test_normal_copy_lineage_for_both_factory_eyes() {
    copy::Context context{};
    for (int eye : {1, 0}) {
        copy::FactoryScope factory(context, &w3vr::engine_camera::remastered_500c,
            input_desc, 123, eye, true, true);
        require(copy::observe_rebuild(context, 0x0228AA4F, scratch_desc + 0x10).stage == copy::Stage::unknown,
            "Rebuild return alone granted a camera route");
        require(run_copy_descriptor(context, 0x01B8067B, scratch_desc, input_desc,
            copy::Stage::scratch, eye, 123), "First descriptor copy rejected");
        require(context.descriptor == nullptr && context.camera == nullptr,
            "Finished scratch leaked camera pointers");
        require(run_copy_descriptor(context, 0x01B80709, final_frame + 0x10, scratch_desc,
            copy::Stage::frame, eye, 123), "Final descriptor copy rejected");
        require(factory.finish(final_frame), "Returned frame does not match final descriptor");
        require(!factory.finish(final_frame), "Factory sequence consumed twice");
    }
    require(context.factory == nullptr && context.descriptor == nullptr && context.camera == nullptr,
        "Factory scope leaked borrowed addresses");
}

void test_unknown_and_nested_copy_scopes_mask_outer_authority() {
    copy::Context context{};
    copy::FactoryScope outer(context, &w3vr::engine_camera::remastered_500c,
        input_desc, 123, 0, true, true);
    {
        copy::DescriptorScope descriptor(context, 0x01B8067B, scratch_desc, input_desc);
        copy::CameraScope camera(context, 0x00324460, scratch_desc + 0x10, input_desc + 0x10);
        {
            copy::FactoryScope rejected(context, &w3vr::engine_camera::legacy_404,
                input_desc, 123, 0, true, true);
            require(copy::observe_rebuild(context, 0x0228AA4F, scratch_desc + 0x10).stage == copy::Stage::unknown,
                "Nested rejected factory inherited outer camera");
        }
        {
            copy::DescriptorScope unknown(context, 0x022C65EC, 0x400000, input_desc);
            copy::CameraScope private_camera(context, 0x00324460, 0x400010, input_desc + 0x10);
            require(copy::observe_rebuild(context, 0x0228AA4F, 0x400010).stage == copy::Stage::unknown,
                "Private copy inherited normal factory identity");
        }
        {
            copy::FactoryScope inner(context, &w3vr::engine_camera::remastered_500c,
                0x700000, 124, 1, true, true);
            require(run_copy_descriptor(context, 0x01B8067B, 0x800000, 0x700000,
                copy::Stage::scratch, 1, 124) &&
                run_copy_descriptor(context, 0x01B80709, 0x900010, 0x800000,
                    copy::Stage::frame, 1, 124) && inner.finish(0x900000),
                "Nested accepted factory mixed outer addresses or eye identity");
        }
        require(context.factory == &outer && context.descriptor == &descriptor && context.camera == &camera,
            "Nested scopes failed to restore exact outer context");
        const auto route = copy::observe_rebuild(context, 0x0228AA4F, scratch_desc + 0x10);
        require(route.stage == copy::Stage::scratch && camera.finish(scratch_desc + 0x10),
            "Nested pass-through invalidated valid outer camera");
        require(run_copy_camera(context, 1, scratch_desc, input_desc, copy::Stage::scratch, 0, 123) &&
            descriptor.finish(scratch_desc), "Outer descriptor could not complete");
    }
    require(run_copy_descriptor(context, 0x01B80709, final_frame + 0x10, scratch_desc,
        copy::Stage::frame, 0, 123) && outer.finish(final_frame), "Nested flow lost lineage");
}

void test_copy_context_rejects_missing_mismatched_and_repeated_steps() {
    for (int failure = 0; failure < 9; ++failure) {
        copy::Context context{};
        copy::FactoryScope factory(context, &w3vr::engine_camera::remastered_500c,
            input_desc, 123, 0, true, true);
        if (failure == 0) {
            copy::DescriptorScope final_first(context, 0x01B80709, final_frame + 0x10, scratch_desc);
            require(final_first.stage == copy::Stage::unknown, "Final copy accepted before scratch");
        } else if (failure == 1) {
            copy::DescriptorScope wrong_source(context, 0x01B8067B, scratch_desc, input_desc + 16);
            require(wrong_source.stage == copy::Stage::unknown, "Scratch accepted another descriptor");
        } else if (failure == 2) {
            copy::DescriptorScope overlapping(context, 0x01B8067B, input_desc + 32, input_desc);
            require(overlapping.stage == copy::Stage::unknown, "Overlapping descriptor copy accepted");
        } else {
            copy::DescriptorScope descriptor(context, 0x01B8067B, scratch_desc, input_desc);
            if (failure == 3) {
                copy::CameraScope reversed(context, 0x00324473, scratch_desc + 0x5F0, input_desc + 0x5F0);
                require(copy::observe_rebuild(context, 0x0228AA4F, scratch_desc + 0x5F0).stage == copy::Stage::unknown,
                    "Second internal record accepted first");
            } else if (failure == 4 || failure == 5) {
                require(!run_copy_camera(context, 0, scratch_desc, input_desc,
                    copy::Stage::scratch, 0, 123, failure == 4, failure == 5),
                    "Missing rebuild or unexpected camera return accepted");
            } else if (failure == 6 || failure == 7) {
                copy::CameraScope camera(context, 0x00324460, scratch_desc + 0x10, input_desc + 0x10);
                const auto target = failure == 6 ? scratch_desc + 0x20 : scratch_desc + 0x10;
                copy::observe_rebuild(context, 0x0228AA4F, target);
                require(copy::observe_rebuild(context, 0x0228AA4F, scratch_desc + 0x10).stage == copy::Stage::unknown &&
                    !camera.finish(scratch_desc + 0x10), "Wrong target or repeated rebuild remained eligible");
            } else {
                require(run_copy_camera(context, 0, scratch_desc, input_desc, copy::Stage::scratch, 0, 123) &&
                    run_copy_camera(context, 1, scratch_desc, input_desc, copy::Stage::scratch, 0, 123),
                    "Fixture camera copy failed");
                require(!descriptor.finish(scratch_desc + 16), "Wrong native descriptor return accepted");
            }
        }
        require(!factory.finish(final_frame), "Incomplete or broken lineage accepted a rendered frame");
    }
    copy::Context context{};
    {
        copy::FactoryScope factory(context, &w3vr::engine_camera::remastered_500c,
            input_desc, 123, 0, true, true);
        require(run_copy_descriptor(context, 0x01B8067B, scratch_desc, input_desc, copy::Stage::scratch, 0, 123),
            "Scratch fixture failed");
        copy::DescriptorScope bad_final(context, 0x01B80709, final_frame + 0x10, input_desc);
        require(bad_final.stage == copy::Stage::unknown && !factory.finish(final_frame),
            "Final copy reused input instead of completed scratch");
    }
    {
        copy::FactoryScope factory(context, &w3vr::engine_camera::remastered_500c,
            input_desc, 123, 0, true, true);
        require(run_copy_descriptor(context, 0x01B8067B, scratch_desc, input_desc, copy::Stage::scratch, 0, 123) &&
            run_copy_descriptor(context, 0x01B80709, final_frame + 0x10, scratch_desc, copy::Stage::frame, 0, 123),
            "Complete fixture failed");
        require(!factory.finish(final_frame + 16), "Native returned frame mismatch accepted");
    }
}

void test_abandoned_copy_scope_restores_context() {
    copy::Context context{};
    {
        copy::FactoryScope factory(context, &w3vr::engine_camera::remastered_500c,
            input_desc, 123, 0, true, true);
        try {
            copy::DescriptorScope descriptor(context, 0x01B8067B, scratch_desc, input_desc);
            copy::CameraScope camera(context, 0x00324460, scratch_desc + 0x10, input_desc + 0x10);
            throw 7; // C++ unwinding only; this is not a native SEH/access-fault test.
        } catch (int) {}
        require(context.factory == &factory && !context.descriptor && !context.camera &&
            factory.phase == copy::Phase::broken && !factory.finish(final_frame),
            "Abandoned native call scope retained pointers or accepted an incomplete copy");
    }
    require(!context.factory && !context.descriptor && !context.camera,
        "Unwound factory retained borrowed addresses");
}

void test_copy_context_admission_and_signatures() {
    copy::Context context{};
    const auto copied = w3vr::engine_camera::remastered_500c;
    for (int failure = 0; failure < 10; ++failure) {
        const auto* contract = failure == 0 ? nullptr : failure == 1 ? &copied :
            failure == 2 ? &w3vr::engine_camera::legacy_404 : &w3vr::engine_camera::remastered_500c;
        copy::FactoryScope factory(context, contract,
            failure == 3 ? 0 : failure == 4 ? UINTPTR_MAX - 15 : input_desc,
            failure == 5 ? 0 : failure == 6 ? UINT64_MAX : 123,
            failure == 7 ? 2 : 0, failure != 8, failure != 9);
        require(!factory.admitted && !factory.finish(final_frame), "Unknown or incomplete producer admission accepted");
    }
    for (const auto* signature : {&copy::descriptor_signature, &copy::camera_signature, &copy::rebuild_signature}) {
        require(copy::signature_matches(*signature, *signature), "Matching native prefix rejected");
        for (std::size_t i = 0; i < signature->size(); ++i) {
            auto wrong = *signature; wrong[i] ^= 0x80;
            require(!copy::signature_matches(*signature, wrong), "Different native prefix accepted");
            require(!copy::signature_matches(*signature, std::span<const std::uint8_t>(*signature).first(i)),
                "Truncated native prefix accepted");
        }
    }
}

int main() {
    test_remastered_native_fixture();
    test_legacy_fixture_and_failed_reads();
    test_selection_and_writer_route_gate();
    test_native_authority_version_and_unknowns();
    test_native_authority_entry_mismatch();
    test_projection_writes_keep_integer_dimensions_and_other_records();
    test_failed_projection_is_not_partially_published();
    test_canted_pose_commits_only_six_floats();
    test_normal_copy_lineage_for_both_factory_eyes();
    test_unknown_and_nested_copy_scopes_mask_outer_authority();
    test_copy_context_rejects_missing_mismatched_and_repeated_steps();
    test_copy_context_admission_and_signatures();
    test_abandoned_copy_scope_restores_context();
}
