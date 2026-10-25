"""Keep GNU constructor metadata separate from the strict XP instruction audit."""

import importlib.util
from pathlib import Path
import shutil
import struct
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / "launcher-prototype" / "check_windows.py"
SPEC = importlib.util.spec_from_file_location("windows_audit", SCRIPT)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)

PE_OFFSET = 0x80
OPTIONAL_SIZE = 0xE0
RAW_OFFSET = 0x200
RAW_SIZE = 0x200
IMAGE_BASE = 0x400000
TEXT_RVA = 0x1000
TEXT_ADDRESS = IMAGE_BASE + TEXT_RVA
TABLE_OFFSET = 0x10
FUNCTION_OFFSET = TABLE_OFFSET - 1
FUNCTION_ADDRESS = TEXT_ADDRESS + FUNCTION_OFFSET
SENTINEL = AUDIT.GNU_LIST_HEADER_SENTINEL
TERMINATOR = AUDIT.GNU_LIST_TERMINATOR
DEFAULT_WORDS = (SENTINEL, FUNCTION_ADDRESS, TERMINATOR)
DEFAULT_SYMBOLS = (("__CTOR_LIST__", TABLE_OFFSET),)
COFF_HEADER_OFFSET = PE_OFFSET + AUDIT.PE_SIGNATURE_SIZE
OPTIONAL_OFFSET = COFF_HEADER_OFFSET + AUDIT.COFF_HEADER_SIZE
SECTION_OFFSET = OPTIONAL_OFFSET + OPTIONAL_SIZE
SECTION_READ_CODE = 0x40000020
CONSOLE_SUBSYSTEM = 3
COFF_NAME_SIZE = AUDIT.COFF_SYMBOL.size - struct.calcsize("<IhHBB")
EXTRA_RVA = 0x400
EXTRA_SIZE = 0x10
EXTRA_FUNCTION_ADDRESS = IMAGE_BASE + EXTRA_RVA + EXTRA_SIZE - 1


def pe_image(words=DEFAULT_WORDS, symbols=DEFAULT_SYMBOLS, suffix=b"\xc3",
             contribution=False, section_flags=AUDIT.SECTION_MEMORY_EXECUTE,
             virtual_size=None):
    code = b"\x90" * FUNCTION_OFFSET + b"\xc3"
    code += b"".join(AUDIT.GNU_PE32_LIST_WORD.pack(word) for word in words) + suffix
    data = bytearray(RAW_OFFSET + RAW_SIZE)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, AUDIT.PE_OFFSET_LOCATION, PE_OFFSET)
    data[PE_OFFSET:COFF_HEADER_OFFSET] = b"PE\0\0"
    struct.pack_into("<HH", data, COFF_HEADER_OFFSET, AUDIT.PE32_MACHINE, 1)
    struct.pack_into("<H", data, COFF_HEADER_OFFSET + AUDIT.COFF_OPTIONAL_SIZE_OFFSET,
                     OPTIONAL_SIZE)
    struct.pack_into("<H", data, OPTIONAL_OFFSET, AUDIT.PE32_MAGIC)
    image_offset, image_format = AUDIT.IMAGE_BASE_OFFSETS[AUDIT.PE32_MAGIC]
    struct.pack_into(image_format, data, OPTIONAL_OFFSET + image_offset, IMAGE_BASE)
    struct.pack_into("<HH", data, OPTIONAL_OFFSET + AUDIT.SUBSYSTEM_VERSION_OFFSET,
                     *AUDIT.MAX_XP_VERSION)
    struct.pack_into("<H", data, OPTIONAL_OFFSET + AUDIT.SUBSYSTEM_OFFSET, CONSOLE_SUBSYSTEM)
    AUDIT.SECTION_HEADER.pack_into(
        data, SECTION_OFFSET, b".text", len(code) if virtual_size is None else virtual_size,
        TEXT_RVA, RAW_SIZE, RAW_OFFSET, 0, 0, 0, 0, section_flags | SECTION_READ_CODE,
    )
    data[RAW_OFFSET:RAW_OFFSET + len(code)] = code
    records = bytearray()
    strings = bytearray(AUDIT.COFF_STRING_TABLE_SIZE)

    def add_symbol(name, value, storage, auxiliary=b""):
        encoded = name.encode("ascii")
        if len(encoded) > COFF_NAME_SIZE:
            encoded = struct.pack("<II", 0, len(strings))
            strings.extend(name.encode("ascii") + b"\0")
        records.extend(AUDIT.COFF_SYMBOL.pack(
            encoded, value, 1, AUDIT.COFF_SECTION_SYMBOL_TYPE, storage, bool(auxiliary),
        ))
        records.extend(auxiliary)

    for name, value in symbols:
        add_symbol(name, value, AUDIT.COFF_EXTERNAL_STORAGE_CLASS)
    if contribution:
        auxiliary = bytearray(AUDIT.COFF_SYMBOL.size)
        struct.pack_into("<I", auxiliary, AUDIT.COFF_AUXILIARY_LENGTH_OFFSET,
                         AUDIT.GNU_PE32_LIST_WORD.size)
        add_symbol(".ctors", TABLE_OFFSET + AUDIT.GNU_PE32_LIST_WORD.size,
                   AUDIT.COFF_STATIC_STORAGE_CLASS, auxiliary)
    struct.pack_into("<II", data, COFF_HEADER_OFFSET + AUDIT.COFF_SYMBOL_TABLE_OFFSET,
                     len(data), len(records) // AUDIT.COFF_SYMBOL.size)
    struct.pack_into("<I", strings, 0, len(strings))
    return bytes(data + records + strings)


def with_extra_section(data, flags=AUDIT.SECTION_MEMORY_EXECUTE, virtual_size=EXTRA_SIZE,
                       file_backed=True):
    data = bytearray(data)
    symbol_field = COFF_HEADER_OFFSET + AUDIT.COFF_SYMBOL_TABLE_OFFSET
    symbol_offset = struct.unpack_from("<I", data, symbol_field)[0]
    if file_backed:
        data[symbol_offset:symbol_offset] = b"\x90" * RAW_SIZE
        struct.pack_into("<I", data, symbol_field, symbol_offset + RAW_SIZE)
        raw_offset = symbol_offset
    else:
        raw_offset = len(data)
    struct.pack_into("<H", data, COFF_HEADER_OFFSET + AUDIT.COFF_SECTION_COUNT_OFFSET, 2)
    AUDIT.SECTION_HEADER.pack_into(
        data, SECTION_OFFSET + AUDIT.SECTION_HEADER.size, b".extra", virtual_size,
        EXTRA_RVA, RAW_SIZE, raw_offset, 0, 0, 0, 0, flags | SECTION_READ_CODE,
    )
    return bytes(data)


class ConstructorMetadataTests(unittest.TestCase):
    def test_complete_lists_do_not_require_per_object_section_auxiliaries(self):
        for symbol in AUDIT.GNU_LIST_HEADER_SYMBOLS:
            for contribution in (False, True):
                with self.subTest(symbol=symbol, contribution=contribution):
                    ranges = AUDIT.read_compiler_data_ranges(pe_image(
                        symbols=((symbol, TABLE_OFFSET),), contribution=contribution))
                    table = next(item for item in ranges if "linker_symbol" in item)
                    self.assertEqual(table["entry_count"], 1)
                    self.assertEqual(table["start_address"], TEXT_ADDRESS + TABLE_OFFSET)
                    self.assertEqual(table["size_bytes"], len(DEFAULT_WORDS)
                                     * AUDIT.GNU_PE32_LIST_WORD.size)

    def test_empty_lists_and_aliases_are_bounded_and_deduplicated(self):
        ranges = AUDIT.read_compiler_data_ranges(pe_image(
            words=(SENTINEL, TERMINATOR),
            symbols=(("__CTOR_LIST__", TABLE_OFFSET), ("___CTOR_LIST__", TABLE_OFFSET))))
        self.assertEqual(len(ranges), 1)
        self.assertEqual(ranges[0]["entry_count"], 0)
        self.assertEqual(ranges[0]["size_bytes"], AUDIT.GNU_PE32_LIST_WORD.size * 2)

    def test_only_recognized_linker_symbols_identify_lists(self):
        for symbols in ((), (("constructor_table", TABLE_OFFSET),)):
            with self.subTest(symbols=symbols):
                self.assertEqual(AUDIT.read_compiler_data_ranges(pe_image(symbols=symbols)), [])

    def test_malformed_or_unbounded_lists_are_rejected(self):
        invalid = (
            (pe_image(words=(TERMINATOR, FUNCTION_ADDRESS, TERMINATOR)), "header contents"),
            (pe_image(words=(SENTINEL, FUNCTION_ADDRESS), suffix=b""), "Unterminated"),
            (pe_image(symbols=(("__CTOR_LIST__", TABLE_OFFSET + 1),)), "Unaligned"),
            (pe_image(symbols=(("__CTOR_LIST__", RAW_SIZE),)), "exceeds its output section"),
            (pe_image(words=(SENTINEL, TEXT_ADDRESS + TABLE_OFFSET, TERMINATOR)),
             "points into proven data"),
            (pe_image(words=(SENTINEL, IMAGE_BASE, TERMINATOR)), "outside executable data"),
            (pe_image(words=(SENTINEL, TEXT_ADDRESS + RAW_SIZE, TERMINATOR)),
             "outside executable data"),
            (pe_image(words=(SENTINEL, TEXT_ADDRESS + RAW_SIZE - 1, TERMINATOR),
                      virtual_size=RAW_SIZE - 1), "outside executable data"),
        )
        for data, message in invalid:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                AUDIT.read_compiler_data_ranges(data)

    def test_missing_terminator_cannot_consume_the_next_list(self):
        next_offset = TABLE_OFFSET + AUDIT.GNU_PE32_LIST_WORD.size * 2
        with self.assertRaisesRegex(ValueError, "Unterminated"):
            AUDIT.read_compiler_data_ranges(pe_image(
                words=(SENTINEL, FUNCTION_ADDRESS, SENTINEL, TERMINATOR),
                symbols=(("__CTOR_LIST__", TABLE_OFFSET), ("__DTOR_LIST__", next_offset))))

    def test_nonexecutable_sections_do_not_create_instruction_exclusions(self):
        self.assertEqual(AUDIT.read_compiler_data_ranges(pe_image(section_flags=0)), [])

    def test_targets_require_file_backed_executable_bytes_in_any_section(self):
        data = pe_image(words=(SENTINEL, EXTRA_FUNCTION_ADDRESS, TERMINATOR))
        ranges = AUDIT.read_compiler_data_ranges(with_extra_section(data))
        self.assertEqual(ranges[0]["entry_count"], 1)
        invalid = (
            with_extra_section(data, flags=0),
            with_extra_section(data, virtual_size=EXTRA_SIZE - 1),
            with_extra_section(data, file_backed=False),
        )
        for image in invalid:
            with self.subTest(image=image[-AUDIT.COFF_SYMBOL.size:]), self.assertRaisesRegex(
                    ValueError, "outside executable data"):
                AUDIT.read_compiler_data_ranges(image)


@unittest.skipUnless(shutil.which("objdump"), "GNU objdump is required for instruction checks")
class InstructionAuditTests(unittest.TestCase):
    def audit_image(self, data, allow_simd=False):
        with tempfile.TemporaryDirectory(prefix="restunts-audit-test-") as temporary:
            binary = Path(temporary) / "fixture.exe"
            binary.write_bytes(data)
            result = AUDIT.audit(binary, "objdump", allow_simd)
            self.assertEqual(binary.read_bytes(), data)
            return result

    def test_constructor_pointer_bytes_are_data_under_both_cpu_policies(self):
        # The pointer begins 0f 10, which objdump decodes as a SIMD instruction
        # unless the complete linker table is masked before disassembly.
        for allow_simd in (False, True):
            with self.subTest(allow_simd=allow_simd):
                result = self.audit_image(pe_image(), allow_simd)
                self.assertTrue(result["passed"], result["problems"])
                self.assertEqual(result["instruction_scan"]["decode_error_count"], 0)
                self.assertEqual(result["instruction_scan"]["simd_instruction_count"], 0)

    def test_undecodable_constructor_pointer_bytes_pass_both_cpu_policies(self):
        # This legitimate constructor address starts with 0f 04, an invalid
        # opcode pair when the table is incorrectly disassembled as code.
        data = with_extra_section(pe_image(
            words=(SENTINEL, EXTRA_FUNCTION_ADDRESS, TERMINATOR)))
        for allow_simd in (False, True):
            with self.subTest(allow_simd=allow_simd):
                result = self.audit_image(data, allow_simd)
                self.assertTrue(result["passed"], result["problems"])
                self.assertEqual(result["instruction_scan"]["decode_error_count"], 0)

    def test_real_simd_immediately_after_terminator_still_fails_cpu_policy(self):
        data = pe_image(suffix=b"\x0f\x10\xc0\xc3")
        result = self.audit_image(data)
        self.assertFalse(result["passed"])
        self.assertEqual(result["instruction_scan"]["simd_instruction_count"], 1)
        self.assertTrue(self.audit_image(data, allow_simd=True)["passed"])

    def test_undecodable_byte_immediately_after_terminator_fails_both_policies(self):
        for allow_simd in (False, True):
            with self.subTest(allow_simd=allow_simd):
                result = self.audit_image(pe_image(suffix=b"\x0f"), allow_simd)
                self.assertFalse(result["passed"])
                self.assertEqual(result["instruction_scan"]["decode_error_count"], 1)

    def test_identical_bytes_without_linker_metadata_are_still_audited(self):
        result = self.audit_image(pe_image(symbols=(("constructor_table", TABLE_OFFSET),)))
        self.assertFalse(result["passed"])
        self.assertEqual(result["instruction_scan"]["excluded_data_bytes"], 0)


if __name__ == "__main__":
    unittest.main()
