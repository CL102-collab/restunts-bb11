#!/usr/bin/env python3
"""Conservatively audit a standalone x86 launcher for common XP/CPU blockers.

This is a static feasibility check, not proof of Windows XP compatibility.
The import checks catch common post-XP APIs; they do not validate every symbol
against XP's exports, dynamically resolved APIs, or code inside system DLLs.
All executable sections are scanned, including guarded runtime code. Recognized
compiler data contributions are excluded only when COFF section-definition
auxiliaries describe their exact byte ranges, even when merged into code. GNU
PE32 constructor/destructor list symbols identify bounded DWORD pointer tables
when their -1 header, null terminator and executable targets are verified.
By default, even unreachable SIMD instructions fail; --allow-simd retains the
XP import and decoding checks for SSE2-enabled x86 builds.
Any undecodable bytes outside proven data make this check fail as well.
"""

import argparse
from bisect import bisect_right
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile


REPORT_VERSION = 2
PE_OFFSET_LOCATION = 0x3C
PE_OFFSET_SIZE = 4
COFF_HEADER_SIZE = 20
PE_SIGNATURE_SIZE = 4
PE32_MACHINE = 0x014C
PE32_MAGIC = 0x010B
PE32_PLUS_MAGIC = 0x020B
MIN_OPTIONAL_HEADER_SIZE = 72
COFF_OPTIONAL_SIZE_OFFSET = 16
COFF_SECTION_COUNT_OFFSET = 2
COFF_SYMBOL_TABLE_OFFSET = 8
COFF_SYMBOL_COUNT_OFFSET = 12
COFF_SYMBOL = struct.Struct("<8sIhHBB")
COFF_STATIC_STORAGE_CLASS = 3
COFF_EXTERNAL_STORAGE_CLASS = 2
COFF_SECTION_SYMBOL_TYPE = 0
COFF_SECTION_AUXILIARY_COUNT = 1
COFF_STRING_TABLE_SIZE = 4
COFF_LONG_NAME_OFFSET = 4
COFF_AUXILIARY_LENGTH_OFFSET = 0
IMAGE_BASE_OFFSETS = {PE32_MAGIC: (28, "<I"), PE32_PLUS_MAGIC: (24, "<Q")}
OS_VERSION_OFFSET = 40
SUBSYSTEM_VERSION_OFFSET = 48
SUBSYSTEM_OFFSET = 68
DATA_DIRECTORY_OFFSETS = {PE32_MAGIC: 96, PE32_PLUS_MAGIC: 112}
DATA_DIRECTORY_COUNT_SIZE = 4
DATA_DIRECTORY_ENTRY_SIZE = 8
IMPORT_DIRECTORY_INDEX = 1
DELAY_IMPORT_DIRECTORY_INDEX = 13
IMPORT_DESCRIPTOR_SIZE = 20
SECTION_HEADER = struct.Struct("<8sIIIIIIHHI")
SECTION_VIRTUAL_ADDRESS_INDEX = 2
SECTION_RAW_SIZE_INDEX = 3
SECTION_RAW_OFFSET_INDEX = 4
SECTION_VIRTUAL_SIZE_INDEX = 1
SECTION_CHARACTERISTICS_INDEX = 9
SECTION_MEMORY_EXECUTE = 0x20000000
X86_NOP = b"\x90"
GNU_PE32_LIST_WORD = struct.Struct("<I")
GNU_LIST_HEADER_SENTINEL = 0xFFFFFFFF
GNU_LIST_TERMINATOR = 0
GNU_LIST_HEADER_SYMBOLS = frozenset({
    "__CTOR_LIST__", "___CTOR_LIST__", "__DTOR_LIST__", "___DTOR_LIST__",
})
MAX_XP_VERSION = (5, 1)
WINDOWS_SUBSYSTEMS = {2: "Windows GUI", 3: "Windows console"}
MAX_TOOL_SECONDS = 120

# A conservative DLL policy for this small launcher, not an XP export database.
# msvcrt.dll is the CRT supplied with XP; external compiler CRTs are rejected.
XP_SYSTEM_DLLS = frozenset(
    """advapi32.dll comctl32.dll comdlg32.dll crypt32.dll gdi32.dll imm32.dll
    iphlpapi.dll kernel32.dll mpr.dll msimg32.dll msvcrt.dll netapi32.dll ntdll.dll
    ole32.dll oleacc.dll oleaut32.dll powrprof.dll psapi.dll rpcrt4.dll secur32.dll
    setupapi.dll shell32.dll shlwapi.dll user32.dll usp10.dll uxtheme.dll version.dll
    wininet.dll winmm.dll winspool.drv wintrust.dll ws2_32.dll""".split()
)
EXTERNAL_RUNTIME_DLL = re.compile(
    r"^(?:api-ms-|ext-ms-|ucrtbase|vcruntime|msvcp|msvcr\d|concrt|"
    r"libstdc\+\+|libgcc|libwinpthread|winpthread|libssp)",
    re.IGNORECASE,
)

# Deliberately common blockers only. Absence from this list does not establish
# that an imported API exists on XP (or a particular XP service pack).
COMMON_POST_XP_IMPORTS = frozenset(
    """AcquireSRWLockExclusive AcquireSRWLockShared AddDllDirectory
    AdjustWindowRectExForDpi CancelIoEx CancelSynchronousIo CompareStringEx
    CompareStringOrdinal CreateFile2 CreateSymbolicLinkA CreateSymbolicLinkW
    EnableNonClientDpiScaling EnumSystemLocalesEx GetDpiForSystem GetDpiForWindow
    GetFileInformationByHandleEx GetFinalPathNameByHandleA GetFinalPathNameByHandleW
    GetLocaleInfoEx GetLogicalProcessorInformationEx GetSystemDefaultLocaleName
    GetSystemTimePreciseAsFileTime GetThreadDescription GetThreadUILanguage
    GetTickCount64 GetUserDefaultLocaleName GetWindowDpiAwarenessContext
    InitOnceBeginInitialize InitOnceComplete InitOnceExecuteOnce InitOnceInitialize
    InitializeConditionVariable InitializeCriticalSectionEx InitializeSRWLock
    LCIDToLocaleName LCMapStringEx LocaleNameToLCID LoadIconMetric LoadIconWithScale
    RegDeleteTreeA RegDeleteTreeW RegGetValueA RegGetValueW ReleaseSRWLockExclusive
    ReleaseSRWLockShared RemoveDllDirectory SetDefaultDllDirectories
    SetFileInformationByHandle SetProcessDPIAware SetProcessDpiAwarenessContext
    SetThreadDescription SetThreadDpiAwarenessContext SHCreateItemFromParsingName
    SHGetKnownFolderIDList SHGetKnownFolderPath SHSetKnownFolderPath
    SleepConditionVariableCS SleepConditionVariableSRW TaskDialog TaskDialogIndirect
    TryAcquireSRWLockExclusive TryAcquireSRWLockShared WakeAllConditionVariable
    WakeConditionVariable""".split()
)

SIMD_REGISTER = re.compile(r"%(?:[xyz]mm\d+|mm[0-7]|k[0-7]|tmm[0-7])\b")
REGISTERLESS_SIMD = frozenset(
    """emms femms ldmxcsr stmxcsr vldmxcsr vstmxcsr lfence mfence sfence
    fxsave fxsave64 fxrstor fxrstor64 xsave xsave64 xsaveopt xsaveopt64
    xsavec xsavec64 xsaves xsaves64 xrstor xrstor64 xrstors xrstors64 xgetbv xsetbv
    prefetch prefetchnta prefetcht0 prefetcht1 prefetcht2 prefetchw prefetchwt1
    prefetchit0 prefetchit1 vzeroupper vzeroall clflush clflushopt clwb cldemote
    monitor mwait monitorx mwaitx movnti movntil movntiq crc32 crc32b crc32w
    crc32l crc32q""".split()
)
INSTRUCTION_PREFIXES = frozenset(
    """addr16 addr32 data16 data32 cs ds es fs gs ss lock rep repe repz repne
    repnz bnd notrack xacquire xrelease""".split()
)
FUNCTION_LINE = re.compile(r"^\s*[0-9a-fA-F]+ <(.+)>:$")
INSTRUCTION_LINE = re.compile(r"^\s*([0-9a-fA-F]+):\s+(.+?)\s*$")
SECTION_LINE = re.compile(r"^Disassembly of section (.+):$")
IMPORT_LINE = re.compile(r"^\s*([0-9a-fA-F]+)\s+(\d+)\s+(\S+)(?:\s+.*)?$")
# GNU PE linker scripts place these compiler data input sections in output
# sections which may also contain code. Suffixes identify COMDAT/ordered inputs.
# Other names, function labels and undecodable bytes never justify exclusions.
COMPILER_DATA_SECTION = re.compile(
    r"^\.(?:gcc_except_table|eh_frame|ctors|dtors|rdata)(?:[.$].*)?$"
)


def read_pe_header(data):
    if len(data) < PE_OFFSET_LOCATION + PE_OFFSET_SIZE or data[:2] != b"MZ":
        raise ValueError("File has no complete DOS/PE header")
    pe_offset = struct.unpack_from("<I", data, PE_OFFSET_LOCATION)[0]
    coff_offset = pe_offset + PE_SIGNATURE_SIZE
    if data[pe_offset:coff_offset] != b"PE\0\0":
        raise ValueError("File has no PE signature")
    if len(data) < coff_offset + COFF_HEADER_SIZE:
        raise ValueError("Truncated COFF header")
    machine, section_count = struct.unpack_from("<HH", data, coff_offset)
    optional_size = struct.unpack_from("<H", data, coff_offset + COFF_OPTIONAL_SIZE_OFFSET)[0]
    optional_offset = coff_offset + COFF_HEADER_SIZE
    if optional_size < MIN_OPTIONAL_HEADER_SIZE or len(data) < optional_offset + optional_size:
        raise ValueError("Truncated PE optional header")
    magic = struct.unpack_from("<H", data, optional_offset)[0]
    os_version = struct.unpack_from("<HH", data, optional_offset + OS_VERSION_OFFSET)
    subsystem_version = struct.unpack_from("<HH", data, optional_offset + SUBSYSTEM_VERSION_OFFSET)
    subsystem = struct.unpack_from("<H", data, optional_offset + SUBSYSTEM_OFFSET)[0]
    header = {
        "machine": f"0x{machine:04x}",
        "optional_header_magic": f"0x{magic:04x}",
        "bits": {PE32_MAGIC: 32, PE32_PLUS_MAGIC: 64}.get(magic),
        "section_count": section_count,
        "os_version": list(os_version),
        "subsystem_version": list(subsystem_version),
        "subsystem": subsystem,
        "subsystem_name": WINDOWS_SUBSYSTEMS.get(subsystem, "other"),
    }
    # Delay-import parsing varies between objdump versions. Do not silently
    # certify an image with an additional import mechanism we have not checked.
    directory_offset = DATA_DIRECTORY_OFFSETS.get(magic)
    header["has_imports"] = False
    header["has_delay_imports"] = False
    import_rva = 0
    if directory_offset is not None and optional_size >= directory_offset:
        directory_count = struct.unpack_from(
            "<I", data, optional_offset + directory_offset - DATA_DIRECTORY_COUNT_SIZE
        )[0]
        for index, name in (
            (IMPORT_DIRECTORY_INDEX, "has_imports"),
            (DELAY_IMPORT_DIRECTORY_INDEX, "has_delay_imports"),
        ):
            directory_end = directory_offset + (index + 1) * DATA_DIRECTORY_ENTRY_SIZE
            if directory_count > index:
                if optional_size < directory_end:
                    raise ValueError("Truncated PE data directory")
                entry_offset = (
                    optional_offset + directory_offset + index * DATA_DIRECTORY_ENTRY_SIZE
                )
                rva, size = struct.unpack_from("<II", data, entry_offset)
                header[name] = bool(rva or size)
                if index == IMPORT_DIRECTORY_INDEX:
                    import_rva = rva
    sections = []
    section_offset = optional_offset + optional_size
    for index in range(section_count):
        offset = section_offset + index * SECTION_HEADER.size
        fields = SECTION_HEADER.unpack_from(data, offset)
        sections.append(
            (
                fields[SECTION_VIRTUAL_ADDRESS_INDEX],
                fields[SECTION_RAW_SIZE_INDEX],
                fields[SECTION_RAW_OFFSET_INDEX],
            )
        )

    def rva_offset(rva):
        for virtual_address, raw_size, raw_offset in sections:
            if virtual_address <= rva < virtual_address + raw_size:
                return raw_offset + rva - virtual_address
        raise ValueError("Import descriptor does not map to file-backed section data")

    descriptor_count = 0
    if import_rva:
        while True:
            offset = rva_offset(import_rva + descriptor_count * IMPORT_DESCRIPTOR_SIZE)
            descriptor = data[offset:offset + IMPORT_DESCRIPTOR_SIZE]
            if len(descriptor) != IMPORT_DESCRIPTOR_SIZE:
                raise ValueError("Truncated PE import descriptor")
            if not any(descriptor):
                break
            descriptor_count += 1
    header["import_descriptor_count"] = descriptor_count
    return header


def run_objdump(executable, arguments, binary=None):
    environment = dict(os.environ, LC_ALL="C", LANG="C")
    command = [executable, *arguments]
    if binary is not None:
        command.append(str(binary))
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env=environment,
        timeout=MAX_TOOL_SECONDS,
    )
    if result.returncode:
        raise ValueError(f"objdump failed ({result.returncode}): {result.stderr.strip()}")
    return result.stdout


def read_compiler_data_ranges(data):
    pe_offset = struct.unpack_from("<I", data, PE_OFFSET_LOCATION)[0]
    coff_offset = pe_offset + PE_SIGNATURE_SIZE
    symbol_offset = struct.unpack_from("<I", data, coff_offset + COFF_SYMBOL_TABLE_OFFSET)[0]
    symbol_count = struct.unpack_from("<I", data, coff_offset + COFF_SYMBOL_COUNT_OFFSET)[0]
    if not symbol_count:
        return []
    symbol_end = symbol_offset + symbol_count * COFF_SYMBOL.size
    if not symbol_offset or symbol_end + COFF_STRING_TABLE_SIZE > len(data):
        raise ValueError("Truncated COFF symbol table")
    string_size = struct.unpack_from("<I", data, symbol_end)[0]
    string_end = symbol_end + string_size
    if string_size < COFF_STRING_TABLE_SIZE or string_end > len(data):
        raise ValueError("Truncated COFF string table")
    section_count = struct.unpack_from("<H", data, coff_offset + COFF_SECTION_COUNT_OFFSET)[0]
    optional_size = struct.unpack_from("<H", data, coff_offset + COFF_OPTIONAL_SIZE_OFFSET)[0]
    optional_offset = coff_offset + COFF_HEADER_SIZE
    magic = struct.unpack_from("<H", data, optional_offset)[0]
    if magic not in IMAGE_BASE_OFFSETS:
        raise ValueError("Unknown PE optional-header format for COFF data ranges")
    image_base_offset, image_base_format = IMAGE_BASE_OFFSETS[magic]
    image_base = struct.unpack_from(image_base_format, data, optional_offset + image_base_offset)[0]
    section_offset = optional_offset + optional_size
    sections = [
        SECTION_HEADER.unpack_from(data, section_offset + index * SECTION_HEADER.size)
        for index in range(section_count)
    ]
    ranges = []
    linker_headers = {}
    index = 0
    while index < symbol_count:
        offset = symbol_offset + index * COFF_SYMBOL.size
        name_bytes, value, section_number, symbol_type, storage, auxiliary_count = (
            COFF_SYMBOL.unpack_from(data, offset)
        )
        next_index = index + auxiliary_count + 1
        if next_index > symbol_count:
            raise ValueError("COFF auxiliary records exceed symbol-table bounds")
        if name_bytes[:COFF_LONG_NAME_OFFSET] == bytes(COFF_LONG_NAME_OFFSET):
            name_offset = struct.unpack_from("<I", name_bytes, COFF_LONG_NAME_OFFSET)[0]
            if not COFF_STRING_TABLE_SIZE <= name_offset < string_size:
                raise ValueError("COFF symbol name is outside the string table")
            start = symbol_end + name_offset
            end = data.find(b"\0", start, string_end)
            if end < 0:
                raise ValueError("Unterminated COFF symbol name")
            name = data[start:end].decode("ascii", errors="replace")
        else:
            name = name_bytes.split(b"\0", 1)[0].decode("ascii", errors="replace")
        if (
            storage == COFF_STATIC_STORAGE_CLASS
            and symbol_type == COFF_SECTION_SYMBOL_TYPE
            and auxiliary_count == COFF_SECTION_AUXILIARY_COUNT
            and COMPILER_DATA_SECTION.fullmatch(name)
            and 0 < section_number <= section_count
        ):
            section = sections[section_number - 1]
            if section[SECTION_CHARACTERISTICS_INDEX] & SECTION_MEMORY_EXECUTE:
                length = struct.unpack_from(
                    "<I", data, offset + COFF_SYMBOL.size + COFF_AUXILIARY_LENGTH_OFFSET
                )[0]
                raw_size = section[SECTION_RAW_SIZE_INDEX]
                virtual_size = section[SECTION_VIRTUAL_SIZE_INDEX] or raw_size
                raw_offset = section[SECTION_RAW_OFFSET_INDEX] + value
                if value + length > min(raw_size, virtual_size) or raw_offset + length > len(data):
                    raise ValueError(f"COFF data contribution exceeds its output section: {name}")
                if length:
                    address = image_base + section[SECTION_VIRTUAL_ADDRESS_INDEX] + value
                    ranges.append({
                        "proof": "COFF section-definition auxiliary",
                        "input_section": name,
                        "output_section_index": section_number,
                        "symbol_index": index,
                        "start_address": address,
                        "end_address": address + length,
                        "file_offset": raw_offset,
                        "size_bytes": length,
                    })
        # GNU i386pe.x emits LONG(-1), constructor/destructor pointers and
        # LONG(0). Some runtime objects retain function symbols but omit .ctors
        # section auxiliaries, so prove the complete list after reading symbols.
        if (
            magic == PE32_MAGIC
            and storage == COFF_EXTERNAL_STORAGE_CLASS
            and symbol_type == COFF_SECTION_SYMBOL_TYPE
            and not auxiliary_count
            and name in GNU_LIST_HEADER_SYMBOLS
            and 0 < section_number <= section_count
        ):
            section = sections[section_number - 1]
            if section[SECTION_CHARACTERISTICS_INDEX] & SECTION_MEMORY_EXECUTE:
                key = (section_number, value)
                linker_headers.setdefault(key, {
                    "linker_symbol": name,
                    "output_section_index": section_number,
                    "symbol_index": index,
                })
        index = next_index
    list_targets = []
    for (section_number, value), header in sorted(linker_headers.items()):
        section = sections[section_number - 1]
        raw_size = section[SECTION_RAW_SIZE_INDEX]
        virtual_size = section[SECTION_VIRTUAL_SIZE_INDEX] or raw_size
        section_size = min(raw_size, virtual_size)
        raw_offset = section[SECTION_RAW_OFFSET_INDEX] + value
        name = header["linker_symbol"]
        if value % GNU_PE32_LIST_WORD.size:
            raise ValueError(f"Unaligned GNU linker list: {name}")
        # A missing terminator must not consume an adjacent linker list.
        limit = min(section_size, min(
            (other_value for other_section, other_value in linker_headers
             if other_section == section_number and other_value > value),
            default=section_size,
        ))
        if (value + GNU_PE32_LIST_WORD.size > section_size
                or raw_offset + GNU_PE32_LIST_WORD.size > len(data)):
            raise ValueError(f"GNU linker list header exceeds its output section: {name}")
        if GNU_PE32_LIST_WORD.unpack_from(data, raw_offset)[0] != GNU_LIST_HEADER_SENTINEL:
            raise ValueError(f"Unexpected GNU linker list header contents: {name}")
        cursor = value + GNU_PE32_LIST_WORD.size
        entry_count = 0
        while True:
            word_offset = section[SECTION_RAW_OFFSET_INDEX] + cursor
            if (cursor + GNU_PE32_LIST_WORD.size > limit
                    or word_offset + GNU_PE32_LIST_WORD.size > len(data)):
                raise ValueError(f"Unterminated GNU linker list: {name}")
            target = GNU_PE32_LIST_WORD.unpack_from(data, word_offset)[0]
            cursor += GNU_PE32_LIST_WORD.size
            if target == GNU_LIST_TERMINATOR:
                break
            if not any(
                target_section[SECTION_CHARACTERISTICS_INDEX] & SECTION_MEMORY_EXECUTE
                and image_base + target_section[SECTION_VIRTUAL_ADDRESS_INDEX] <= target
                < image_base + target_section[SECTION_VIRTUAL_ADDRESS_INDEX] + min(
                    target_section[SECTION_RAW_SIZE_INDEX],
                    target_section[SECTION_VIRTUAL_SIZE_INDEX]
                    or target_section[SECTION_RAW_SIZE_INDEX],
                )
                and target_section[SECTION_RAW_OFFSET_INDEX]
                + target - image_base - target_section[SECTION_VIRTUAL_ADDRESS_INDEX] < len(data)
                for target_section in sections
            ):
                raise ValueError(f"GNU linker list target is outside executable data: {name}")
            list_targets.append((name, target))
            entry_count += 1
        address = image_base + section[SECTION_VIRTUAL_ADDRESS_INDEX] + value
        ranges.append(dict(header,
            proof="GNU PE32 list symbol, verified sentinels and executable targets",
            start_address=address,
            end_address=image_base + section[SECTION_VIRTUAL_ADDRESS_INDEX] + cursor,
            file_offset=raw_offset,
            size_bytes=cursor - value,
            entry_count=entry_count,
        ))
    ranges.sort(key=lambda item: item["start_address"])
    intervals = merged_data_intervals(ranges)
    interval_starts = [start for start, _ in intervals]
    for name, target in list_targets:
        interval = bisect_right(interval_starts, target) - 1
        if interval >= 0 and target < intervals[interval][1]:
            raise ValueError(f"GNU linker list target points into proven data: {name}")
    return ranges


def merged_data_intervals(ranges):
    intervals = []
    for item in ranges:
        start, end = item["start_address"], item["end_address"]
        if intervals and start <= intervals[-1][1]:
            intervals[-1] = (intervals[-1][0], max(end, intervals[-1][1]))
        else:
            intervals.append((start, end))
    return intervals


def read_imports(output):
    imports = []
    current = None
    in_members = False
    for line in output.splitlines():
        if "DLL Name:" in line:
            current = {"dll": line.split("DLL Name:", 1)[1].strip(), "symbols": [], "ordinals": []}
            imports.append(current)
            in_members = False
        elif "Hint/Ord" in line and "Member-Name" in line:
            in_members = current is not None
        elif in_members:
            member = IMPORT_LINE.match(line)
            if member:
                symbol = member.group(3)
                if symbol == "<none>":
                    current["ordinals"].append(int(member.group(2)))
                else:
                    current["symbols"].append(symbol)
            elif line.strip():
                in_members = False
    return imports


def scan_instructions(output, data_ranges=()):
    section = None
    function = None
    sections = []
    functions = set()
    instruction_count = 0
    findings = []
    decode_errors = []
    intervals = merged_data_intervals(data_ranges)
    interval_starts = [start for start, _ in intervals]
    excluded_instruction_count = 0
    for line in output.splitlines():
        section_match = SECTION_LINE.match(line)
        if section_match:
            section = section_match.group(1)
            sections.append(section)
            function = None
            continue
        function_match = FUNCTION_LINE.match(line)
        if function_match:
            function = function_match.group(1)
            functions.add((section, function))
            continue
        instruction = INSTRUCTION_LINE.match(line)
        if not instruction:
            continue
        address = int(instruction.group(1), 16)
        interval = bisect_right(interval_starts, address) - 1
        if interval >= 0 and address < intervals[interval][1]:
            excluded_instruction_count += 1
            continue
        instruction_count += 1
        text = instruction.group(2).split("#", 1)[0].strip()
        # Symbol annotations can contain arbitrary names. Inspect actual
        # operands, not names such as a function called "xmm0".
        text = re.sub(r"<[^>]*>", "", text).strip()
        words = text.split()
        while words and (words[0] in INSTRUCTION_PREFIXES or words[0].startswith("rex")):
            words.pop(0)
        mnemonic = words[0].lower() if words else ""
        operands = " ".join(words[1:])
        location = {
            "address": f"0x{instruction.group(1)}",
            "section": section,
            "function": function,
            "mnemonic": mnemonic,
            "operands": operands,
        }
        if "(bad)" in text or mnemonic in {".byte", ".word", ".long"}:
            decode_errors.append(location)
        registers = sorted(set(SIMD_REGISTER.findall(operands)))
        reasons = []
        if registers:
            reasons.append("MMX/SIMD register operand: " + ", ".join(registers))
        if mnemonic in REGISTERLESS_SIMD:
            reasons.append("SIMD, cache/prefetch or extended-state instruction")
        if reasons:
            findings.append(dict(location, reasons=reasons))
    return {
        "instruction_count": instruction_count,
        "function_count": len(functions),
        "sections": sections,
        "simd_instruction_count": len(findings),
        "simd_mnemonic_counts": dict(
            sorted(Counter(x["mnemonic"] for x in findings).items())
        ),
        "findings": findings,
        "decode_error_count": len(decode_errors),
        "decode_errors": decode_errors,
        "excluded_data_range_count": len(data_ranges),
        "excluded_data_bytes": sum(end - start for start, end in intervals),
        "excluded_data_instruction_count": excluded_instruction_count,
        "excluded_data_ranges": list(data_ranges),
    }


def audit(binary, objdump, allow_simd=False):
    report = {
        "report_version": REPORT_VERSION,
        "forbid_simd": not allow_simd,
        "binary": str(binary.resolve()),
        "objdump": objdump,
        "passed": False,
        "problems": [],
        "limitations": [
            "Static feasibility audit only; actual Windows XP/no-SSE2 execution is still required.",
            "The post-XP import list covers common blockers, not all XP exports or service packs.",
            "Dynamically resolved APIs and instructions inside imported DLLs are not validated.",
            ("SIMD is allowed; this report does not certify a CPU instruction floor."
             if allow_simd else
             "SIMD in guarded runtime paths also fails this conservative instruction policy."),
            "Compiler data exclusions require bounded COFF section-definition auxiliaries.",
            "GNU PE32 lists require linker symbols, verified sentinels and executable targets.",
            "Any undecodable bytes outside proven data fail, including stripped embedded data.",
        ],
    }
    problems = report["problems"]
    try:
        data = binary.read_bytes()
        report["size_bytes"] = len(data)
        report["sha256"] = hashlib.sha256(data).hexdigest()
        header = read_pe_header(data)
        report["pe"] = header
        report["objdump_version"] = run_objdump(objdump, ["--version"]).splitlines()[0]
        if header["machine"] != f"0x{PE32_MACHINE:04x}" or header["bits"] != 32:
            problems.append("Expected a 32-bit x86 PE32 executable")
        if tuple(header["subsystem_version"]) > MAX_XP_VERSION:
            problems.append("PE subsystem version exceeds Windows XP 5.1")
        if header["subsystem"] not in WINDOWS_SUBSYSTEMS:
            problems.append("Expected a Windows GUI or console subsystem")
        if header["has_delay_imports"]:
            problems.append(
                "Delay imports require a separate audit; this checker does not certify them"
            )

        imports = read_imports(run_objdump(objdump, ["-p"], binary))
        report["imports"] = imports
        report["import_dll_count"] = len(imports)
        report["import_symbol_count"] = sum(len(item["symbols"]) for item in imports)
        report["import_ordinal_count"] = sum(len(item["ordinals"]) for item in imports)
        if header["import_descriptor_count"] != len(imports):
            problems.append("objdump DLL count does not match the PE import descriptors")
        for item in imports:
            dll = item["dll"]
            if EXTERNAL_RUNTIME_DLL.match(dll):
                problems.append(f"External compiler/API-set runtime dependency: {dll}")
            elif dll.lower() not in XP_SYSTEM_DLLS:
                problems.append(
                    f"DLL outside the launcher's conservative XP system allowlist: {dll}"
                )
            for symbol in item["symbols"]:
                if symbol in COMMON_POST_XP_IMPORTS:
                    problems.append(f"Common post-XP import: {dll}!{symbol}")
            if item["ordinals"]:
                problems.append(f"Ordinal imports require manual XP export validation: {dll}")
            if not item["symbols"] and not item["ordinals"]:
                problems.append(f"Could not decode imported members for {dll}")

        data_ranges = read_compiler_data_ranges(data)
        # Mask only proven data in a temporary snapshot. One-byte NOPs preserve
        # decoding alignment immediately after data, including a real SIMD
        # instruction there. Original executable bytes and its hash are intact.
        with tempfile.TemporaryDirectory(prefix="restunts-pe-audit-") as temporary:
            snapshot = bytearray(data)
            for item in data_ranges:
                offset, size = item["file_offset"], item["size_bytes"]
                snapshot[offset:offset + size] = X86_NOP * size
            disassembly_binary = Path(temporary) / binary.name
            disassembly_binary.write_bytes(snapshot)
            scan = scan_instructions(
                run_objdump(objdump, ["-d", "--no-show-raw-insn"], disassembly_binary), data_ranges
            )
        report["instruction_scan"] = scan
        if not scan["instruction_count"]:
            problems.append("objdump produced no decoded instructions")
        if scan["simd_instruction_count"] and not allow_simd:
            problems.append(f"Found {scan['simd_instruction_count']} MMX/SIMD/state instructions")
        if scan["decode_error_count"]:
            problems.append(
                f"Could not decode {scan['decode_error_count']} instructions outside proven data"
            )
    except (OSError, ValueError, struct.error, subprocess.TimeoutExpired) as error:
        problems.append(str(error))
    report["passed"] = not problems
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Windows executable to inspect")
    parser.add_argument("--objdump", default="objdump", help="GNU objdump executable")
    parser.add_argument("--output", type=Path, help="Also save the complete JSON report here")
    parser.add_argument("--quiet", action="store_true", help="Print only the outcome and problems")
    parser.add_argument("--allow-simd", action="store_true",
                        help="Audit XP imports while allowing SIMD in SSE2-enabled x86 builds")
    arguments = parser.parse_args()
    report = audit(arguments.binary, arguments.objdump, arguments.allow_simd)
    serialized = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    if arguments.output:
        try:
            arguments.output.write_text(serialized, encoding="utf-8")
        except OSError as error:
            parser.exit(1, f"Cannot save audit report: {error}\n")
    if arguments.quiet:
        outcome = "PASS" if report["passed"] else "FAIL"
        print(f"{outcome}: {report['binary']}")
        for problem in report["problems"]:
            print(f"  {problem}")
    else:
        sys.stdout.write(serialized)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
