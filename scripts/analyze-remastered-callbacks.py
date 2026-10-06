"""Offline investigation; never executes, modifies or copies the game binary.

Install the pinned analysis dependency separately, in .analysis-packages.
Candidate registration references are evidence to review, not a runtime port.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / ".analysis-packages"))
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_GRP_JUMP
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG, X86_REG_RIP, X86_REG_RAX


def ensure_report_destination(destination: Path, *inputs: Path):
    target = destination.resolve()
    for source in inputs:
        if target == source.resolve() or (destination.exists() and source.exists()
                                          and destination.samefile(source)):
            raise ValueError("Report destination would overwrite an input file")


class Image:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError("Not a Windows executable")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        machine, count = struct.unpack_from("<HH", self.data, pe + 4)
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        optional = pe + 24
        if machine != 0x8664 or struct.unpack_from("<H", self.data, optional)[0] != 0x20B:
            raise ValueError("Requires a 64-bit x86 PE image")
        self.base = struct.unpack_from("<Q", self.data, optional + 24)[0]
        self.sections = []
        for index in range(count):
            section = optional + optional_size + index * 40
            name = self.data[section:section + 8].rstrip(b"\0").decode("ascii")
            _, va, size, raw = struct.unpack_from("<IIII", self.data, section + 8)
            flags = struct.unpack_from("<I", self.data, section + 36)[0]
            if raw + size > len(self.data):
                raise ValueError("Truncated section")
            self.sections.append((name, va, size, raw, flags))
        exception_rva, exception_size = struct.unpack_from("<II", self.data, optional + 112 + 3 * 8)
        self.functions = []
        self.unwind_records = {}
        for offset in range(self.offset(exception_rva), self.offset(exception_rva) + exception_size, 12):
            begin, end, unwind = struct.unpack_from("<III", self.data, offset)
            if begin < end:
                self.functions.append((begin, end))
                self.unwind_records[begin] = (end, unwind)
        self.functions.sort()
        self.starts = [begin for begin, _ in self.functions]
        self._primary_entries = {}
        self._function_chunks = None
        self.decoder = Cs(CS_ARCH_X86, CS_MODE_64)
        self.decoder.detail = True

    def offset(self, rva: int) -> int:
        for _, va, size, raw, _ in self.sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError(f"RVA outside file sections: {rva:#x}")

    def rva(self, offset: int) -> int:
        for _, va, size, raw, _ in self.sections:
            if raw <= offset < raw + size:
                return va + offset - raw
        raise ValueError("Offset outside sections")

    def function(self, rva: int):
        index = bisect.bisect_right(self.starts, rva) - 1
        if index >= 0:
            begin, end = self.functions[index]
            if begin <= rva < end:
                return begin, end
        return None

    def instructions(self, begin: int, end: int):
        offset = self.offset(begin)
        return list(self.decoder.disasm(self.data[offset:offset + end - begin], begin))

    def read(self, rva: int, size: int) -> bytes:
        """Read a complete range from one file-backed section, without zero fill."""
        for _, va, length, raw, _ in self.sections:
            if size >= 0 and va <= rva and rva + size <= va + length:
                offset = raw + rva - va
                if offset + size <= len(self.data):
                    return self.data[offset:offset + size]
        raise ValueError(f"Range outside file-backed sections: {rva:#x}+{size:#x}")

    def executable(self, rva: int) -> bool:
        return any(va <= rva < va + size and flags & 0x20000000
                   for _, va, size, _, flags in self.sections)

    def primary_function_rva(self, rva: int):
        """Follow checked UNW_FLAG_CHAININFO records to the primary entry.

        A .pdata range may start in the middle of a function. Only the final
        primary record supplies an entry candidate. Never join intervening
        bytes or silently accept malformed/cyclic chains.
        https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64
        """
        bounds = self.function(rva)
        if bounds is None:
            return None
        current = bounds[0]
        visited = []
        while current not in self._primary_entries:
            if current in visited or len(visited) >= 64:
                raise ValueError("Cyclic or excessively long unwind chain")
            visited.append(current)
            end, unwind = self.unwind_records[current]
            version_flags, _, count, _ = self.read(unwind, 4)
            flags = version_flags >> 3
            version = version_flags & 7
            # The game also contains three unchained version-2 CRT records.
            # Their header identifies a primary range; epilogue codes need not
            # be decoded here. Do not claim support for a version-2 chain.
            if version not in {1, 2} or flags & ~7 or (version == 2 and flags & 4):
                raise ValueError("Unsupported unwind format")
            if not flags & 4:
                self._primary_entries[current] = current
                break
            if flags & 3:
                raise ValueError("Chained unwind record also declares a handler")
            chain_rva = unwind + 4 + ((count + 1) & ~1) * 2
            parent, parent_end, parent_unwind = struct.unpack("<III", self.read(chain_rva, 12))
            if self.unwind_records.get(parent) != (parent_end, parent_unwind):
                raise ValueError("Unwind chain does not match an indexed runtime function")
            if not self.executable(parent):
                raise ValueError("Unwind parent is outside executable sections")
            current = parent
        primary = self._primary_entries[current]
        for fragment in visited:
            self._primary_entries[fragment] = primary
        return primary

    def function_chunks(self, rva: int):
        primary = self.primary_function_rva(rva)
        if primary is None:
            return []
        if self._function_chunks is None:
            chunks = {}
            for begin, end in self.functions:
                entry = self.primary_function_rva(begin)
                chunks.setdefault(entry, []).append((begin, end))
            self._function_chunks = chunks
        return self._function_chunks[primary]

    def reachable_instructions(self, rva: int):
        """Bounded control-flow decode; never decode data after a return as code.

        Seed primary and checked unwind fragment entries, follow direct branches
        inside those ranges, but don't guess targets of indirect jumps. This is
        deliberately incomplete when a jump table hasn't been resolved.
        """
        chunks = self.function_chunks(rva)
        starts = [begin for begin, _ in chunks]
        pending = list(starts)
        decoded = {}
        indirect = set()
        failures = set()
        while pending:
            address = pending.pop()
            while address not in decoded:
                index = bisect.bisect_right(starts, address) - 1
                if index < 0 or not chunks[index][0] <= address < chunks[index][1]:
                    break
                end = chunks[index][1]
                instruction = next(self.decoder.disasm(self.read(address, min(15, end - address)),
                                                      address, count=1), None)
                if instruction is None:
                    failures.add(address)
                    break
                decoded[address] = instruction
                if instruction.mnemonic in {"ret", "retf", "iret", "iretd", "iretq", "int3", "ud2", "hlt"}:
                    break
                if instruction.group(CS_GRP_JUMP):
                    operand = instruction.operands[0]
                    if operand.type == X86_OP_IMM:
                        pending.append(operand.imm)
                    else:
                        indirect.add(address)
                    if instruction.mnemonic == "jmp":
                        break
                address += instruction.size
        return {"Instructions": [decoded[address] for address in sorted(decoded)],
                "UnresolvedIndirectJumps": sorted(indirect), "DecodeFailures": sorted(failures)}

    def occurrences(self, value: bytes):
        start = 0
        while (start := self.data.find(value, start)) >= 0:
            try:
                yield self.rva(start)
            except ValueError:
                pass
            start += 1

    def rip_references(self, targets: set[int]):
        """Verify LEA/MOV candidates at decoded instruction boundaries.

        Raw pattern searches alone can mistake instruction data for code.
        Decode each enclosing unwind function once before accepting a match.
        """
        possible_functions = set()
        pattern = re.compile(rb"(?=([\x48-\x4f][\x8d\x8b\x89][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....))", re.DOTALL)
        for _, va, size, raw, flags in self.sections:
            if not flags & 0x20000000:
                continue
            for match in pattern.finditer(self.data, raw, raw + size):
                offset = match.start()
                rva = va + offset - raw
                target = rva + 7 + struct.unpack_from("<i", self.data, offset + 3)[0]
                if target in targets:
                    function = self.function(rva)
                    if function:
                        possible_functions.add(function)
        references = []
        primary_entries = {self.primary_function_rva(begin) for begin, _ in possible_functions}
        for begin in sorted(primary_entries):
            for insn in self.reachable_instructions(begin)["Instructions"]:
                for operand in insn.operands:
                    if operand.type == X86_OP_MEM and operand.mem.base == X86_REG_RIP:
                        target = insn.address + insn.size + operand.mem.disp
                        if target in targets:
                            references.append({"TargetRva": f"0x{target:08X}",
                                               "InstructionRva": f"0x{insn.address:08X}",
                                               "FunctionRva": f"0x{begin:08X}",
                                               "Instruction": f"{insn.mnemonic} {insn.op_str}"})
        return references


def registration_candidates(image: Image, strings: dict, references: list):
    """Recognize the reviewed name + nearby callback-pointer registration shape.

    Keep multiple same-name registrations instead of guessing their owner type.
    A candidate must be a decoded LEA into RAX into executable code, and the
    address must be saved in a memory operand before RAX is overwritten.
    Leaf callbacks may lack unwind records; label those separately.
    This is offline evidence, never an automatic compatibility acceptance.
    """
    result = {}
    cache = {}
    for name, name_addresses in strings.items():
        records = []
        for ref in references:
            if int(ref["TargetRva"], 16) not in name_addresses:
                continue
            name_rva = int(ref["InstructionRva"], 16)
            bounds = image.function(name_rva)
            if bounds not in cache:
                cache[bounds] = image.reachable_instructions(bounds[0])["Instructions"]
            instructions = cache[bounds]
            for index, insn in enumerate(instructions):
                if not name_rva - 80 <= insn.address < name_rva or insn.mnemonic != "lea":
                    continue
                if len(insn.operands) != 2:
                    continue
                dest, source = insn.operands
                if dest.type != X86_OP_REG or dest.reg != X86_REG_RAX or source.type != X86_OP_MEM or source.mem.base != X86_REG_RIP:
                    continue
                target = insn.address + insn.size + source.mem.disp
                callback_bounds = image.function(target)
                indexed_start = bool(callback_bounds and image.primary_function_rva(target) == target)
                if callback_bounds and not indexed_start:
                    continue
                executable_target = any(va <= target < va + size and flags & 0x20000000
                    for _, va, size, _, flags in image.sections)
                if not executable_target:
                    continue
                store = None
                for later in instructions[index + 1:]:
                    if later.address > name_rva + 24:
                        break
                    if later.mnemonic == "mov" and len(later.operands) == 2:
                        dst, src = later.operands
                        if dst.type == X86_OP_MEM and src.type == X86_OP_REG and src.reg == X86_REG_RAX:
                            store = later
                            break
                    _, written = later.regs_access()
                    if X86_REG_RAX in written or later.mnemonic in {"call", "jmp", "ret"}:
                        break
                if store is not None:
                    if indexed_start:
                        start, end = callback_bounds
                        length_basis = "UnwindRecord"
                    else:
                        start = target
                        offset = image.offset(start)
                        preview = list(image.decoder.disasm(image.data[offset:offset + 64], start))
                        returns = [i for i in preview if i.mnemonic == "ret"]
                        if not returns:
                            continue
                        end = returns[0].address + returns[0].size
                        length_basis = "FirstReturnWithin64Bytes_NoUnwindRecord"
                    offset = image.offset(start)
                    records.append({"CandidateRva": f"0x{target:08X}",
                        "RegistrationFunctionRva": f"0x{image.primary_function_rva(name_rva):08X}",
                        "NameReferenceRva": f"0x{name_rva:08X}",
                        "PointerLoadRva": f"0x{insn.address:08X}",
                        "PointerStoreRva": f"0x{store.address:08X}",
                        "UnwindFunctionStartsHere": indexed_start,
                        "LengthBasis": length_basis,
                        "FunctionBytes": end - start,
                        "FunctionSha256": hashlib.sha256(image.data[offset:offset + end - start]).hexdigest()})
        result[name] = {"Candidates": records, "Ambiguous": len({r["CandidateRva"] for r in records}) > 1}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    ensure_report_destination(args.report, args.exe)
    image = Image(args.exe)
    names = ["GetHeadBoneIndex", "GetBoneWorldMatrixByIndex", "IsInGameplayScene",
             "IsInNonGameplayCutscene", "IsCurrentlyPlayingNonGameplayScene",
             "EnableManualControl", "GetTopmostCameraObject", "IsLoadingScreenVideoPlaying",
             "GetCameraDirection", "WorldVectorToViewRatio", "IsAnyMenu", "GetCurrentViewportResolution"]
    candidates = {"GetHeadBoneIndex": 0x02102690, "GetBoneWorldMatrixByIndex": 0x02251A40}
    targets = set(candidates.values())
    strings = {}
    for name in names:
        hits = set(image.occurrences(name.encode("ascii") + b"\0"))
        hits.update(image.occurrences(name.encode("utf-16-le") + b"\0\0"))
        strings[name] = sorted(hits)
        targets.update(hits)
    references = image.rip_references(targets)
    callback_facts = {}
    for name, rva in candidates.items():
        function = image.function(rva)
        facts = {"CandidateRva": f"0x{rva:08X}", "UnwindFunctionStartsHere": bool(function and function[0] == rva)}
        if function:
            instructions = image.instructions(*function)
            facts["FunctionBytes"] = function[1] - function[0]
            facts["FunctionSha256"] = hashlib.sha256(image.data[image.offset(function[0]):image.offset(function[0]) + function[1] - function[0]]).hexdigest()
            facts["DirectCalls"] = [f"0x{i.operands[0].imm:08X}" for i in instructions
                                     if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM]
            facts["RipDataReferences"] = sorted({f"0x{i.address + i.size + operand.mem.disp:08X}"
                for i in instructions for operand in i.operands
                if operand.type == X86_OP_MEM and operand.mem.base == X86_REG_RIP})
        function_refs = [r for r in references if int(r["TargetRva"], 16) == rva]
        string_functions = {r["FunctionRva"] for r in references if int(r["TargetRva"], 16) in strings[name]}
        facts["AddressReferences"] = function_refs
        facts["RegistrationFunctionsAlsoReferencingExactName"] = sorted({r["FunctionRva"] for r in function_refs} & string_functions)
        callback_facts[name] = facts
    report = {"ExecutableSha256": hashlib.sha256(image.data).hexdigest().upper(),
              "ImageBase": f"0x{image.base:X}", "Callbacks": callback_facts,
              "RegistrationCandidates": registration_candidates(image, strings, references),
              "NameStrings": {name: [f"0x{rva:08X}" for rva in values] for name, values in strings.items()},
              "References": references,
              "RuntimePortEnabled": False,
              "Note": "Static registration evidence only; no game or headset validation and no binary changes."}
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"RegisteredCandidates": {name: info for name, info in report["RegistrationCandidates"].items()},
                      "VerifiedReferences": len(references)}, indent=2))


if __name__ == "__main__":
    main()
