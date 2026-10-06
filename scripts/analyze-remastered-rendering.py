"""Read-only RTTI and rendering call-graph evidence, never a runtime port.

Uses the pinned Capstone dependency of analyze-remastered-callbacks.py.
Does not launch the game, load its DLLs, patch bytes or emit game binaries.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

_spec = importlib.util.spec_from_file_location(
    "w3vr_callback_analysis", Path(__file__).with_name("analyze-remastered-callbacks.py"))
_callbacks = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_callbacks)
Image = _callbacks.Image

from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP


def hx(value):
    return f"0x{value:08X}"


def find_rtti_tables(image, class_names):
    """Validate x64 MSVC relative COL records and their absolute table links.

    A string hit alone is insufficient: require a matching self RVA, type
    descriptor RVA, signature, aligned table and executable method pointers.
    Stop before adjacent RTTI/data instead of treating those words as methods.
    Secondary subobjects retain their offset and are never a primary vtable.
    """
    tables = []
    for class_name in class_names:
        decorated = f".?AV{class_name}@@".encode("ascii") + b"\0"
        for name_rva in image.occurrences(decorated):
            type_rva = name_rva - 16
            for type_link in image.occurrences(struct.pack("<I", type_rva)):
                col_rva = type_link - 12
                if col_rva % 4:
                    continue
                try:
                    signature, offset, cd_offset, typ, hierarchy, self_rva = struct.unpack(
                        "<6I", image.read(col_rva, 24))
                    if signature != 1 or self_rva != col_rva or typ != type_rva:
                        continue
                    image.read(type_rva, 16 + len(decorated))
                    image.read(hierarchy, 16)
                except ValueError:
                    continue
                for table_link in image.occurrences(struct.pack("<Q", image.base + col_rva)):
                    if table_link % 8:
                        continue
                    vtable = table_link + 8
                    methods = []
                    for slot in range(128):
                        try:
                            target = struct.unpack("<Q", image.read(vtable + slot * 8, 8))[0] - image.base
                        except ValueError:
                            break
                        if not image.executable(target):
                            break
                        methods.append({"Slot": slot, "MethodRva": hx(target),
                                        "PrimaryEntryRva": hx(entry) if (
                                            entry := image.primary_function_rva(target)) is not None else None})
                    if methods:
                        tables.append({"Class": class_name, "NameRva": hx(name_rva),
                            "TypeDescriptorRva": hx(type_rva), "LocatorRva": hx(col_rva),
                            "HierarchyRva": hx(hierarchy), "SubobjectOffset": offset,
                            "ConstructionDisplacementOffset": cd_offset,
                            "VtableRva": hx(vtable), "Methods": methods,
                            "MethodScanCapped": len(methods) == 128})
    return tables


def callback_owners(callback_report, tables):
    result = {}
    for name, info in callback_report["RegistrationCandidates"].items():
        candidates = []
        for callback in info["Candidates"]:
            registration = callback["RegistrationFunctionRva"]
            owners = sorted({table["Class"] for table in tables
                if table["SubobjectOffset"] == 0 and table["Class"].endswith("ClassBuilder")
                and any(method["MethodRva"] == registration for method in table["Methods"])})
            candidates.append({"CallbackRva": callback["CandidateRva"],
                "RegistrationFunctionRva": registration, "OwnerBuilderClasses": owners,
                "OwnerUniquelyIdentified": len(owners) == 1,
                "RuntimeVerified": False})
        result[name] = candidates
    return result


def direct_references(image, targets):
    """Verify E8/E9 references by decoding the enclosing unwind range.

    A matching displacement inside other instruction data is not a call.
    Lookahead permits overlapping byte candidates; only decoded boundaries
    decide. Call sites without unwind metadata are not guessed.
    """
    ranges = set()
    for _, va, size, raw, flags in image.sections:
        if not flags & 0x20000000:
            continue
        for match in re.finditer(rb"(?=([\xe8\xe9]....))", image.data[raw:raw + size], re.DOTALL):
            offset = raw + match.start()
            rva = va + match.start()
            target = rva + 5 + struct.unpack_from("<i", image.data, offset + 1)[0]
            if target in targets and (bounds := image.function(rva)):
                ranges.add(bounds)
    references = []
    entries = {image.primary_function_rva(begin) for begin, _ in ranges}
    for entry in sorted(entries):
        for insn in image.reachable_instructions(entry)["Instructions"]:
            if (insn.mnemonic in {"call", "jmp"} and insn.operands
                    and insn.operands[0].type == X86_OP_IMM and insn.operands[0].imm in targets):
                references.append({"InstructionRva": hx(insn.address), "Kind": insn.mnemonic,
                    "TargetRva": hx(insn.operands[0].imm),
                    "UnwindRangeBeginRva": hx(image.function(insn.address)[0]),
                    "CallerPrimaryEntryRva": hx(image.primary_function_rva(insn.address))})
    return references


def function_facts(image, rva):
    entry = image.primary_function_rva(rva)
    if entry is None:
        return {"RequestedRva": hx(rva), "PrimaryEntryRva": None,
                "Note": "No unwind record; no entry inferred from neighbouring bytes."}
    chunks = image.function_chunks(entry)
    flow = image.reachable_instructions(entry)
    instructions = flow["Instructions"]
    decoded_bytes = sum(insn.size for insn in instructions)
    total_bytes = sum(end - begin for begin, end in chunks)
    calls = []
    rip = []
    offsets = set()
    for insn in instructions:
        if insn.mnemonic in {"call", "jmp"} and insn.operands and insn.operands[0].type == X86_OP_IMM:
            target = insn.operands[0].imm
            calls.append({"InstructionRva": hx(insn.address), "Kind": insn.mnemonic,
                "TargetRva": hx(target), "ExecutableTarget": image.executable(target),
                "TargetPrimaryEntryRva": hx(primary) if image.executable(target) and (
                    primary := image.primary_function_rva(target)) is not None else None})
        for operand in insn.operands:
            if operand.type != X86_OP_MEM:
                continue
            memory = operand.mem
            if memory.base == X86_REG_RIP:
                rip.append({"InstructionRva": hx(insn.address),
                            "TargetRva": hx(insn.address + insn.size + memory.disp),
                            "Operation": insn.mnemonic})
            elif memory.base and memory.disp and insn.reg_name(memory.base) not in {"rsp", "rbp"}:
                # Register offsets are observations, not verified object fields.
                offsets.add((insn.reg_name(memory.base), memory.disp, operand.size))
    return {"RequestedRva": hx(rva), "PrimaryEntryRva": hx(entry),
        "RequestedAddressIsPrimaryEntry": entry == rva,
        "Chunks": [{"BeginRva": hx(begin), "EndRva": hx(end), "Bytes": end - begin,
                    "Sha256": hashlib.sha256(image.read(begin, end - begin)).hexdigest()}
                   for begin, end in chunks],
        "TotalChunkBytes": total_bytes, "ReachedInstructionBytes": decoded_bytes,
        "UnreachedChunkBytes": total_bytes - decoded_bytes,
        "UnresolvedIndirectJumps": [hx(address) for address in flow["UnresolvedIndirectJumps"]],
        "DecodeFailures": [hx(address) for address in flow["DecodeFailures"]],
        "DirectControlTransfers": calls, "RipReferences": rip,
        "ObservedRegisterOffsets": [{"Register": reg, "Offset": offset, "Bytes": size}
                                    for reg, offset, size in sorted(offsets)],
        "RuntimeVerified": False, "ObjectLayoutVerified": False}


def direct_call_leaf_facts(image, call_rva, max_bytes=4096, max_instructions=2048):
    """Inspect a reached direct-call target that has no unwind record.

    Windows x64 leaf routines need not have .pdata, but arbitrary neighbouring
    bytes are not an entry. Require a decoded CALL in a checked caller first.
    Follow conditional paths in a bounded executable window; stop at jumps
    instead of assuming a tail target belongs to the same routine. Refuse
    calls, stack/nonvolatile register changes and overlapping instructions.
    This is static evidence only, never permission to execute a new game hook.
    """
    caller = image.primary_function_rva(call_rva)
    if caller is None or not 1 <= max_bytes <= 65536 or not 1 <= max_instructions <= 16384:
        raise ValueError("Leaf inspection requires a checked caller and bounded budgets")
    call = next((i for i in image.reachable_instructions(caller)["Instructions"]
                 if i.address == call_rva), None)
    if (call is None or call.mnemonic != "call" or not call.operands
            or call.operands[0].type != X86_OP_IMM):
        raise ValueError("Requested address is not a reached direct CALL")
    entry = call.operands[0].imm
    if not image.executable(entry) or image.function(entry) is not None:
        raise ValueError("Target must be executable and have no unwind record")
    section = next((s for s in image.sections if s[1] <= entry < s[1] + s[2]), None)
    end = min(entry + max_bytes, section[1] + section[2])
    # Never cross a recorded function, even if it happens to be in the budget.
    end = min([end] + [begin for begin in image.starts if entry < begin < end])
    pending, decoded, occupied = [entry], {}, {}
    problems, jumps, returns, offsets = [], [], [], set()
    preserved = {"rbx", "ebx", "bx", "bl", "bh", "rbp", "ebp", "bp", "bpl",
                 "rdi", "edi", "di", "dil", "rsi", "esi", "si", "sil",
                 "rsp", "esp", "sp", "spl"}
    preserved.update(f"r{n}{suffix}" for n in range(12, 16) for suffix in ["", "d", "w", "b"])
    preserved.update(f"{kind}{n}" for kind in ["xmm", "ymm", "zmm"] for n in range(6, 16))
    while pending:
        address = pending.pop()
        while address not in decoded:
            if not entry <= address < end:
                problems.append({"Rva": hx(address), "Reason": "Outside bounded leaf window"})
                break
            if address in occupied:
                problems.append({"Rva": hx(address), "Reason": "Branch into instruction bytes"})
                break
            if len(decoded) >= max_instructions:
                problems.append({"Rva": hx(address), "Reason": "Instruction budget exhausted"})
                break
            try:
                insn = next(image.decoder.disasm(image.read(address, min(15, end - address)),
                                                 address, count=1), None)
            except ValueError:
                insn = None
            if insn is None:
                problems.append({"Rva": hx(address), "Reason": "Cannot decode file-backed instruction"})
                break
            if any(byte in occupied for byte in range(address, address + insn.size)):
                problems.append({"Rva": hx(address), "Reason": "Overlapping instruction paths"})
                break
            decoded[address] = insn
            occupied.update((byte, address) for byte in range(address, address + insn.size))
            if insn.mnemonic == "ret" and not insn.operands:
                returns.append(hx(address))
                break
            written = {insn.reg_name(reg) for reg in insn.regs_access()[1]}
            if (written & preserved or insn.mnemonic in
                    {"call", "enter", "leave", "push", "pop", "ret", "retf", "iret", "iretd", "iretq"}):
                problems.append({"Rva": hx(address), "Reason": "Not a stack-preserving x64 leaf"})
                break
            if insn.mnemonic in {"int3", "ud2", "hlt", "syscall", "sysenter", "int"}:
                problems.append({"Rva": hx(address), "Reason": "Non-returning or unsupported terminator"})
                break
            for operand in insn.operands:
                if operand.type == X86_OP_MEM and operand.mem.base:
                    offsets.add((insn.reg_name(operand.mem.base), operand.mem.disp, operand.size))
            if insn.mnemonic == "jmp":
                jumps.append({"Rva": hx(address), "TargetRva": hx(insn.operands[0].imm)
                              if insn.operands[0].type == X86_OP_IMM else None})
                break
            if insn.group(_callbacks.CS_GRP_JUMP):
                if not insn.operands or insn.operands[0].type != X86_OP_IMM:
                    problems.append({"Rva": hx(address), "Reason": "Unresolved conditional target"})
                    break
                pending.append(insn.operands[0].imm)
            address += insn.size
    return {"CallInstructionRva": hx(call_rva), "CallerPrimaryEntryRva": hx(caller),
        "ValidatedDirectCallTargetRva": hx(entry), "PrimaryEntryRva": None,
        "BoundedWindowEndRva": hx(end), "ReachedInstructionBytes": sum(i.size for i in decoded.values()),
        "Instructions": [{"Rva": hx(i.address), "Operation": i.mnemonic,
                          "Operands": i.op_str} for i in sorted(decoded.values(), key=lambda i: i.address)],
        "ReturnSites": sorted(returns), "StoppedJumps": jumps, "Problems": problems,
        "CompleteLeafControlFlow": bool(returns) and not problems and not jumps,
        "ObservedRegisterOffsets": [{"Register": reg, "Offset": offset, "Bytes": size}
                                    for reg, offset, size in sorted(offsets)],
        "RuntimeVerified": False, "ObjectLayoutVerified": False,
        "Note": "Reached direct-call entry only; no unwind range or semantic object layout inferred."}


def build_report(image, callback_report, inspect_rvas, call_depth=1):
    digest = hashlib.sha256(image.data).hexdigest().upper()
    if callback_report["ExecutableSha256"].upper() != digest:
        raise ValueError("Callback report belongs to another executable")
    classes = ["CRenderFrame", "CRenderInterface", "CRenderSceneEx",
               "CRenderCommand_RenderScene", "CCameraDirector", "CR4CameraDirector",
               "CCustomCamera", "CCameraDirectorClassBuilder", "CCameraClassBuilder",
               "CCustomCameraClassBuilder"]
    tables = find_rtti_tables(image, classes)
    references = image.rip_references({int(t["VtableRva"], 16) for t in tables})
    for ref in references:
        ref["PrimaryEntryRva"] = hx(image.primary_function_rva(int(ref["InstructionRva"], 16)))
    owners = callback_owners(callback_report, tables)
    roots = set(inspect_rvas)
    frame_tables = {table["VtableRva"] for table in tables if table["Class"] == "CRenderFrame"}
    roots.update(int(ref["PrimaryEntryRva"], 16) for ref in references if ref["TargetRva"] in frame_tables)
    for table in tables:
        if table["Class"] == "CRenderCommand_RenderScene" and table["SubobjectOffset"] == 0:
            roots.update(int(method["MethodRva"], 16) for method in table["Methods"])
    roots.update(int(candidate["CallbackRva"], 16)
                 for candidates in owners.values() for candidate in candidates)
    facts = {}
    pending = [(rva, 0) for rva in sorted(roots)]
    while pending:
        rva, depth = pending.pop(0)
        primary = image.primary_function_rva(rva)
        key = hx(primary if primary is not None else rva)
        if key in facts:
            continue
        if len(facts) >= 256:
            raise ValueError("Call-graph budget exceeded; reduce --call-depth")
        fact = function_facts(image, rva)
        facts[key] = fact
        if depth < call_depth:
            pending.extend((int(call["TargetPrimaryEntryRva"], 16), depth + 1)
                for call in fact.get("DirectControlTransfers", [])
                if call["Kind"] == "call" and call["TargetPrimaryEntryRva"])
    return {"ExecutableSha256": digest, "ImageBase": hx(image.base),
        "RttiTables": tables, "VtableCodeReferences": references,
        "CallbackOwnerCandidates": owners, "RootFunctions": [hx(rva) for rva in sorted(roots)],
        "CallDepth": call_depth, "Functions": facts,
        "InspectedFunctionCallers": direct_references(image, set(inspect_rvas)),
        "RuntimePortEnabled": False, "RenderingVerified": False,
        "Note": "Static class identity, chained unwind chunks and bounded control-flow references only. "
                "Indirect jump-table targets are not guessed, so call graphs may be incomplete. "
                "Function roles, signatures, object layouts and runtime behaviour require review."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--callbacks", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--inspect-rva", type=lambda value: int(value, 0), action="append", default=[])
    parser.add_argument("--call-depth", type=int, choices=range(3), default=1)
    parser.add_argument("--inspect-leaf-call", type=lambda value: int(value, 0), action="append", default=[],
                        help="Reached direct CALL to an executable target without unwind metadata")
    args = parser.parse_args()
    _callbacks.ensure_report_destination(args.report, args.exe, args.callbacks)
    image = Image(args.exe)
    callback_report = json.loads(args.callbacks.read_text(encoding="utf-8-sig"))
    report = build_report(image, callback_report, args.inspect_rva, args.call_depth)
    report["DirectCallLeafFunctions"] = [direct_call_leaf_facts(image, rva)
                                         for rva in args.inspect_leaf_call]
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"RttiTables": len(report["RttiTables"]),
        "Functions": len(report["Functions"]), "VtableReferences": len(report["VtableCodeReferences"]),
        "CameraDirectionOwners": report["CallbackOwnerCandidates"]["GetCameraDirection"],
        "RuntimePortEnabled": False}, indent=2))


if __name__ == "__main__":
    main()
