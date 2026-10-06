"""Synthetic executable metadata tests; no game file or headset is needed."""
import importlib.util
from pathlib import Path
import struct
import unittest

_spec = importlib.util.spec_from_file_location("w3vr_render_analysis",
    Path(__file__).resolve().parents[1] / "scripts" / "analyze-remastered-rendering.py")
analysis = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(analysis)


def fixture():
    image = analysis.Image.__new__(analysis.Image)
    image.base = 0x140000000
    image.data = bytearray(0x800)
    image.sections = [(".text", 0x1000, 0x200, 0, 0x20000000),
                      (".rdata", 0x2000, 0x600, 0x200, 0)]
    image.unwind_records = {0x1000: (0x1010, 0x2000),
                            0x1080: (0x1090, 0x2010),
                            0x1100: (0x1110, 0x2020)}
    image.functions = sorted((begin, end) for begin, (end, _) in image.unwind_records.items())
    image.starts = [begin for begin, _ in image.functions]
    image._primary_entries = {}
    image._function_chunks = None
    image.decoder = analysis._callbacks.Cs(analysis._callbacks.CS_ARCH_X86,
                                          analysis._callbacks.CS_MODE_64)
    image.decoder.detail = True
    for rva in [0x2000, 0x2010]:
        struct.pack_into("<4B", image.data, image.offset(rva), 1, 0, 0, 0)
    struct.pack_into("<4B", image.data, image.offset(0x2020), 1 | 4 << 3, 0, 1, 0)
    # Odd code counts must include the unused alignment slot before the chain.
    struct.pack_into("<III", image.data, image.offset(0x2028), 0x1000, 0x1010, 0x2000)
    return image


def add_type(image, subobject_offset=0):
    name = b".?AVExampleClassBuilder@@\0"
    image.data[image.offset(0x2050):image.offset(0x2050) + len(name)] = name
    struct.pack_into("<6I", image.data, image.offset(0x2080), 1, subobject_offset,
                     0, 0x2040, 0x20C0, 0x2080)
    struct.pack_into("<Q", image.data, image.offset(0x20D8), image.base + 0x2080)
    struct.pack_into("<3Q", image.data, image.offset(0x20E0), image.base + 0x1000,
                     image.base + 0x1100, image.base + 0x2080)


class UnwindTests(unittest.TestCase):
    def test_fragment_maps_to_primary_and_preserves_discontinuous_chunks(self):
        image = fixture()
        self.assertEqual(image.primary_function_rva(0x1105), 0x1000)
        self.assertEqual(image.function_chunks(0x1105), [(0x1000, 0x1010), (0x1100, 0x1110)])
        self.assertEqual(image.primary_function_rva(0x1088), 0x1080)

    def test_multiple_chains_follow_primary(self):
        image = fixture()
        image.data[image.offset(0x2010)] = 1 | 4 << 3
        struct.pack_into("<III", image.data, image.offset(0x2014), 0x1100, 0x1110, 0x2020)
        self.assertEqual(image.primary_function_rva(0x1080), 0x1000)

    def test_leaf_has_no_inferred_entry(self):
        self.assertIsNone(fixture().primary_function_rva(0x1150))

    def test_cycle_rejected(self):
        image = fixture()
        image.data[image.offset(0x2000)] = 1 | 4 << 3
        struct.pack_into("<III", image.data, image.offset(0x2004), 0x1100, 0x1110, 0x2020)
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            image.primary_function_rva(0x1100)

    def test_changed_parent_range_rejected(self):
        image = fixture()
        struct.pack_into("<I", image.data, image.offset(0x202C), 0x1011)
        with self.assertRaisesRegex(ValueError, "indexed"):
            image.primary_function_rva(0x1100)

    def test_handler_and_chain_are_incompatible(self):
        image = fixture()
        image.data[image.offset(0x2020)] = 1 | 5 << 3
        with self.assertRaisesRegex(ValueError, "handler"):
            image.primary_function_rva(0x1100)

    def test_truncated_chain_rejected(self):
        image = fixture()
        image.data = image.data[:image.offset(0x2030)]
        with self.assertRaisesRegex(ValueError, "outside"):
            image.primary_function_rva(0x1100)

    def test_unchained_v2_header_and_unknown_version(self):
        image = fixture()
        image.data[image.offset(0x2000)] = 2
        self.assertEqual(image.primary_function_rva(0x1005), 0x1000)
        image = fixture()
        image.data[image.offset(0x2000)] = 3
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            image.primary_function_rva(0x1005)

    def test_v2_chain_is_not_supported(self):
        image = fixture()
        image.data[image.offset(0x2020)] = 2 | 4 << 3
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            image.primary_function_rva(0x1100)

    def test_read_cannot_cross_section_or_zero_fill(self):
        image = fixture()
        with self.assertRaises(ValueError):
            image.read(0x11FF, 2)
        with self.assertRaises(ValueError):
            image.read(0x2000, -1)


class IdentityTests(unittest.TestCase):
    def test_report_cannot_overwrite_executable(self):
        game = Path("analysis-fixture.exe")
        with self.assertRaisesRegex(ValueError, "overwrite"):
            analysis._callbacks.ensure_report_destination(game, game)

    def test_report_cannot_overwrite_callback_input(self):
        callbacks = Path("analysis-callbacks.json")
        with self.assertRaisesRegex(ValueError, "overwrite"):
            analysis._callbacks.ensure_report_destination(callbacks, Path("fixture.exe"), callbacks)

    def test_vtable_stops_before_locator_and_reports_fragment_entry(self):
        image = fixture()
        add_type(image)
        tables = analysis.find_rtti_tables(image, ["ExampleClassBuilder"])
        self.assertEqual(len(tables), 1)
        self.assertEqual(len(tables[0]["Methods"]), 2)
        self.assertEqual(tables[0]["Methods"][1]["PrimaryEntryRva"], "0x00001000")

    def test_type_name_alone_does_not_identify_vtable(self):
        image = fixture()
        add_type(image)
        struct.pack_into("<I", image.data, image.offset(0x2094), 0x2084)
        self.assertEqual(analysis.find_rtti_tables(image, ["ExampleClassBuilder"]), [])

    def test_secondary_vtable_is_not_a_builder_owner(self):
        image = fixture()
        add_type(image, 16)
        tables = analysis.find_rtti_tables(image, ["ExampleClassBuilder"])
        report = {"RegistrationCandidates": {"SameName": {"Candidates": [{
            "CandidateRva": "0x00001080", "RegistrationFunctionRva": "0x00001000"}]}}}
        owner = analysis.callback_owners(report, tables)["SameName"][0]
        self.assertEqual(owner["OwnerBuilderClasses"], [])
        self.assertFalse(owner["OwnerUniquelyIdentified"])

    def test_same_callback_name_keeps_distinct_class_owners(self):
        tables = [{"Class": name, "SubobjectOffset": 0,
                   "Methods": [{"MethodRva": registration}]}
                  for name, registration in [("DirectorClassBuilder", "0x00001000"),
                                              ("CameraClassBuilder", "0x00001080")]]
        report = {"RegistrationCandidates": {"SameName": {"Candidates": [
            {"CandidateRva": "0x00001200", "RegistrationFunctionRva": "0x00001000"},
            {"CandidateRva": "0x00001300", "RegistrationFunctionRva": "0x00001080"}]}}}
        owners = analysis.callback_owners(report, tables)["SameName"]
        self.assertEqual(owners[0]["OwnerBuilderClasses"], ["DirectorClassBuilder"])
        self.assertEqual(owners[1]["OwnerBuilderClasses"], ["CameraClassBuilder"])

    def test_report_from_another_executable_is_rejected(self):
        image = fixture()
        with self.assertRaisesRegex(ValueError, "another executable"):
            analysis.build_report(image, {"ExecutableSha256": "0" * 64}, [])


class ControlFlowTests(unittest.TestCase):
    def test_rip_store_finds_global_pointer_initialization(self):
        image = fixture()
        image.data[:8] = b"\x48\x89\x05" + struct.pack("<i",0x2050-0x1007) + b"\xc3"
        refs=image.rip_references({0x2050})
        self.assertEqual([ref["InstructionRva"] for ref in refs],["0x00001000"])
        self.assertTrue(refs[0]["Instruction"].startswith("mov qword ptr [rip"))

    def test_rip_store_after_return_is_not_a_reference(self):
        image=fixture()
        image.data[:9] = b"\xc3\x48\x89\x05" + struct.pack("<i",0x2050-0x1008) + b"\xc3"
        self.assertEqual(image.rip_references({0x2050}),[])

    def test_rip_store_inside_immediate_is_not_a_reference(self):
        image=fixture()
        image.data[:11] = b"\x48\xb8\x48\x89\x05" + struct.pack("<i",0x2050-0x1009) + b"\x90\xc3"
        self.assertEqual(image.rip_references({0x2050}),[])

    def test_fake_call_after_return_is_not_code(self):
        image = fixture()
        offset = image.offset(0x1000)
        image.data[offset] = 0xC3
        image.data[offset + 1] = 0xE8
        struct.pack_into("<i", image.data, offset + 2, 0x1080 - 0x1006)
        self.assertEqual(analysis.direct_references(image, {0x1080}), [])

    def test_operand_bytes_do_not_count_as_calls(self):
        image = fixture()
        offset = image.offset(0x1000)
        # MOV EAX,imm32 contains E8 + part of a false displacement. The next
        # instruction supplies its final byte. A real CALL follows at +8.
        image.data[offset:offset + 14] = bytes.fromhex("b8e87a000000c090e873000000c3")
        refs = analysis.direct_references(image, {0x1080})
        self.assertEqual([ref["InstructionRva"] for ref in refs], ["0x00001008"])

    def test_both_conditional_paths_but_no_dead_embedded_data(self):
        image = fixture()
        offset = image.offset(0x1000)
        image.data[offset:offset + 3] = bytes.fromhex("7406c3")
        for address in [0x1003, 0x1008]:
            image.data[image.offset(address)] = 0xE8
            struct.pack_into("<i", image.data, image.offset(address + 1), 0x1080 - address - 5)
        image.data[image.offset(0x100D)] = 0xC3
        refs = analysis.direct_references(image, {0x1080})
        self.assertEqual([ref["InstructionRva"] for ref in refs], ["0x00001008"])

    def test_indirect_jump_is_unresolved_and_does_not_decode_table(self):
        image = fixture()
        image.data[:7] = bytes.fromhex("ffe0e879000000")
        flow = image.reachable_instructions(0x1000)
        self.assertEqual(flow["UnresolvedIndirectJumps"], [0x1000])
        self.assertEqual(analysis.direct_references(image, {0x1080}), [])

    def test_call_from_unwind_fragment_records_real_primary(self):
        image = fixture()
        image.data[image.offset(0x1000)] = 0xC3
        image.data[image.offset(0x1100):image.offset(0x1100) + 6] = bytes.fromhex("e87bffffffc3")
        refs = analysis.direct_references(image, {0x1080})
        self.assertEqual(len(refs), 1)
        self.assertEqual(refs[0]["CallerPrimaryEntryRva"], "0x00001000")
        self.assertEqual(refs[0]["UnwindRangeBeginRva"], "0x00001100")


if __name__ == "__main__":
    unittest.main()
