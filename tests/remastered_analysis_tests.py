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


def switch_fixture():
    image = fixture()
    image.unwind_records[0x1000] = (0x1070, 0x2000)
    # Update the indexed parent of the chained fragment too.
    struct.pack_into("<III", image.data, image.offset(0x2028), 0x1000, 0x1070, 0x2000)
    image.functions = sorted((b, e) for b, (e, _) in image.unwind_records.items())
    image.starts = [b for b, _ in image.functions]
    code = (bytes.fromhex("83f802771b488d15") + struct.pack("<i", -0x100C)
            + bytes.fromhex("8b8c82") + struct.pack("<I", 0x1030)
            + bytes.fromhex("4803caffe1"))
    image.data[:len(code)] = code
    for address in [0x1020, 0x1080, 0x1100]:
        image.data[image.offset(address)] = 0xC3
    for address in [0x1040, 0x1048]:
        image.data[image.offset(address):image.offset(address) + 6] = (
            b"\xe8" + struct.pack("<i", 0x1080 - address - 5) + b"\xc3")
    struct.pack_into("<3I", image.data, image.offset(0x1030), 0x1040, 0x1048, 0x1040)
    return image


class BoundedSwitchTests(unittest.TestCase):
    def test_calls_inside_verified_cases_are_reached_without_decoding_table_data(self):
        image = switch_fixture()
        flow = image.reachable_instructions(0x1000)
        self.assertEqual(flow["UnresolvedIndirectJumps"], [])
        self.assertEqual(flow["ResolvedJumpTables"][0]["Targets"], [0x1040, 0x1048, 0x1040])
        self.assertFalse(any(0x1030 <= x.address < 0x103C for x in flow["Instructions"]))
        self.assertEqual([r["InstructionRva"] for r in analysis.direct_references(image, {0x1080})],
                         ["0x00001040", "0x00001048"])

    def test_wrong_guard_or_nonzero_base_remains_unresolved(self):
        for offset, value in [(3, 0x76), (8, (-0x100C + 1) & 0xFF)]:
            image = switch_fixture()
            image.data[offset] = value
            flow = image.reachable_instructions(0x1000)
            self.assertEqual(flow["ResolvedJumpTables"], [])
            self.assertEqual(analysis.direct_references(image, {0x1080}), [])

    def test_target_from_different_owner_or_inside_table_is_rejected(self):
        for target in [0x1080, 0x1034, 0x9000]:
            image = switch_fixture()
            struct.pack_into("<I", image.data, image.offset(0x1030), target)
            self.assertEqual(image.reachable_instructions(0x1000)["ResolvedJumpTables"], [])

    def test_table_crossing_file_backed_section_is_rejected(self):
        image = switch_fixture()
        struct.pack_into("<I", image.data, 15, 0x11FC)
        self.assertEqual(image.reachable_instructions(0x1000)["ResolvedJumpTables"], [])

    def test_middle_of_instruction_cannot_be_an_additional_case(self):
        image = switch_fixture()
        struct.pack_into("<I", image.data, image.offset(0x1034), 0x1041)
        with self.assertRaisesRegex(ValueError, "overlaps an instruction"):
            image.reachable_instructions(0x1000)

    def test_table_cannot_overlap_reached_instructions(self):
        image = switch_fixture()
        # Both targets are initially valid boundaries, but the table bytes
        # themselves are reached from the default edge.
        image.data[4] = 0x2B  # JA 0x1030
        with self.assertRaisesRegex(ValueError, "overlap"):
            image.reachable_instructions(0x1000)


class DirectCallLeafTests(unittest.TestCase):
    def leaf_image(self, code=b"\xc3", target=0x1150):
        image = fixture()
        image.data[:6] = b"\xe8" + struct.pack("<i", target - 0x1005) + b"\xc3"
        if target == 0x1150:
            offset = image.offset(target)
            image.data[offset:offset + len(code)] = code
        return image

    def test_checked_call_and_both_reference_retention_paths(self):
        # MOV pointer; store; conditional LOCK INC through it; RET.
        image = self.leaf_image(bytes.fromhex("488b024889014885c07403f0ff00c3"))
        fact = analysis.direct_call_leaf_facts(image, 0x1000)
        self.assertEqual(fact["ValidatedDirectCallTargetRva"], "0x00001150")
        self.assertIsNone(fact["PrimaryEntryRva"])
        self.assertTrue(fact["CompleteLeafControlFlow"])
        self.assertEqual(fact["ReturnSites"], ["0x0000115E"])
        self.assertFalse(fact["ObjectLayoutVerified"])
        self.assertFalse(fact["RuntimeVerified"])
        self.assertIsNone(image.primary_function_rva(0x1150))

    def test_does_not_decode_after_leaf_return(self):
        fact = analysis.direct_call_leaf_facts(self.leaf_image(bytes.fromhex("c3e800000000")), 0x1000)
        self.assertEqual(len(fact["Instructions"]), 1)
        self.assertTrue(fact["CompleteLeafControlFlow"])

    def test_call_after_caller_return_is_rejected(self):
        image = self.leaf_image()
        image.data[:7] = b"\xc3\xe8" + struct.pack("<i", 0x1150 - 0x1006) + b"\xc3"
        with self.assertRaisesRegex(ValueError, "reached direct CALL"):
            analysis.direct_call_leaf_facts(image, 0x1001)

    def test_call_inside_immediate_is_rejected(self):
        image = self.leaf_image()
        image.data[:11] = bytes.fromhex("48b8e800000000000000c3")
        with self.assertRaisesRegex(ValueError, "reached direct CALL"):
            analysis.direct_call_leaf_facts(image, 0x1002)

    def test_non_executable_or_recorded_target_is_rejected(self):
        for target in [0x2000, 0x1080]:
            with self.subTest(target=target), self.assertRaisesRegex(ValueError, "Target must"):
                analysis.direct_call_leaf_facts(self.leaf_image(target=target), 0x1000)

    def test_callsite_without_unwind_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "checked caller"):
            analysis.direct_call_leaf_facts(self.leaf_image(), 0x1150)

    def test_stack_and_nonvolatile_integer_vector_writes_are_rejected(self):
        for code in ["4883ec08c3", "4889c3c3", "0f28f0c3", "4154c3", "e800000000c3", "c20800"]:
            with self.subTest(code=code):
                fact = analysis.direct_call_leaf_facts(self.leaf_image(bytes.fromhex(code)), 0x1000)
                self.assertFalse(fact["CompleteLeafControlFlow"])
                self.assertEqual(fact["Problems"][0]["Reason"], "Not a stack-preserving x64 leaf")

    def test_branch_into_instruction_is_rejected(self):
        fact = analysis.direct_call_leaf_facts(self.leaf_image(bytes.fromhex("b80102030474fac3")), 0x1000)
        self.assertFalse(fact["CompleteLeafControlFlow"])
        self.assertEqual(fact["Problems"][0]["Reason"], "Branch into instruction bytes")

    def test_jump_is_reported_without_following_target(self):
        for code in ["e980000000c3", "ffe0c3"]:
            with self.subTest(code=code):
                fact = analysis.direct_call_leaf_facts(self.leaf_image(bytes.fromhex(code)), 0x1000)
                self.assertFalse(fact["CompleteLeafControlFlow"])
                self.assertEqual(len(fact["Instructions"]), 1)
                self.assertEqual(len(fact["StoppedJumps"]), 1)
                self.assertEqual(fact["Problems"], [])

    def test_out_of_window_conditional_path_is_not_complete(self):
        fact = analysis.direct_call_leaf_facts(self.leaf_image(bytes.fromhex("74fdc3")), 0x1000)
        self.assertFalse(fact["CompleteLeafControlFlow"])
        self.assertEqual(fact["Problems"][0]["Reason"], "Outside bounded leaf window")

    def test_budgets_are_enforced(self):
        image = self.leaf_image(bytes.fromhex("9090c3"))
        fact = analysis.direct_call_leaf_facts(image, 0x1000, max_instructions=1)
        self.assertEqual(fact["Problems"][0]["Reason"], "Instruction budget exhausted")
        fact = analysis.direct_call_leaf_facts(image, 0x1000, max_bytes=1)
        self.assertEqual(fact["Problems"][0]["Reason"], "Outside bounded leaf window")
        for budget in [0, 65537]:
            with self.assertRaises(ValueError):
                analysis.direct_call_leaf_facts(image, 0x1000, max_bytes=budget)

    def test_truncated_file_and_next_unwind_range_stop_decode(self):
        image = self.leaf_image(bytes.fromhex("488b02488901c3"))
        image.data = image.data[:image.offset(0x1151)]
        with self.assertRaisesRegex(ValueError, "outside file-backed"):
            analysis.direct_call_leaf_facts(image, 0x1000)
        image = self.leaf_image(bytes.fromhex("488b02488901c3"))
        image.sections[0] = (".text", 0x1000, 0x151, 0, 0x20000000)
        fact = analysis.direct_call_leaf_facts(image, 0x1000)
        self.assertFalse(fact["CompleteLeafControlFlow"])
        self.assertEqual(fact["Problems"][0]["Reason"], "Cannot decode file-backed instruction")
        image = self.leaf_image(bytes.fromhex("488b02488901c3"))
        image.starts.append(0x1152)
        image.starts.sort()
        fact = analysis.direct_call_leaf_facts(image, 0x1000)
        self.assertEqual(fact["BoundedWindowEndRva"], "0x00001152")
        self.assertFalse(fact["CompleteLeafControlFlow"])


if __name__ == "__main__":
    unittest.main()
