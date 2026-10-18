#!/usr/bin/env python3
"""Build a host C oracle and matching HolyC tests for translation semantics."""
import argparse
import importlib.machinery
import importlib.util
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
import tempfile

HERE = Path(__file__).resolve().parent
LOADER = importlib.machinery.SourceFileLoader("temple_translate", str(HERE / "TRANSLATE.PY"))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
TRANSLATE = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(TRANSLATE)
A = TRANSLATE.A

# Mixed-case C locals, globals, and members exercise the HolyC name conversion
# while the host oracle verifies the same arithmetic and storage semantics.
FIXTURE = r'''
#include "legacy.h"
#include <stdarg.h>
enum TranslationEnum { TranslationNegative = -1 };
enum TranslationStorageEnum {
    TRANSLATION_STORAGE_INITIAL = -1,
    TRANSLATION_STORAGE_STEP = 2,
    TRANSLATION_STORAGE_EXPECTED = 1,
    TRANSLATION_STORAGE_FAILURE = 51,
    TRANSLATION_VARIADIC_FAILURE = 52
};
struct TranslationPair { legacy_u16 first; legacy_s16 second; };
struct TranslationPacked { legacy_s8 value; legacy_u8 neighbor; legacy_u16 word; legacy_u32 doubleWord; };
static struct TranslationPacked translationPacked = {8, 56, 4660, 3735928559U};
struct TranslationUnion { union { legacy_u32 unsigned_value; legacy_s32 signed_value; }; };
static legacy_u16 translation_zero_words[7];
static legacy_u16 translation_zero;
static legacy_s32 translation_effects;
static legacy_s32 TranslationEffect(void) { translation_effects++; return 1; }
static legacy_s32 TranslationShortReturn(void) { return 1 || TranslationEffect(); }
static legacy_u16 translation_partial[4] = {23};
static struct TranslationPair translation_pairs[2] = {{1, 2}, {3, -4}};
static struct TranslationPair *TranslationPointer(void) { return &translation_pairs[1]; }
static struct TranslationPair TranslationResult(legacy_s16 value)
{
    struct TranslationPair result = {7, value};
    return result;
}
static struct TranslationPair TranslationArgument(struct TranslationPair value)
{
    value.first++;
    value.second--;
    return value;
}
static legacy_s8 TranslationByteResult(void) { return -1; }
static legacy_s16 TranslationNarrow(legacy_s32 value) { return value; }
static legacy_s32 TranslationArg(legacy_s16 value) { return value; }
static legacy_s16 TranslationInc(legacy_s16 value) { return value + 1; }
static legacy_s16 TranslationDec(legacy_s16 value) { return value - 1; }
static legacy_s16 (*translation_callbacks[2])(legacy_s16) = {TranslationInc, TranslationDec};
static legacy_s32 TranslationNarrowStorage(void)
{
    legacy_u8 byte = (legacy_u8)TRANSLATION_STORAGE_INITIAL;
    legacy_s8 signedByte = TRANSLATION_STORAGE_INITIAL;
    legacy_u16 word = (legacy_u16)TRANSLATION_STORAGE_INITIAL;
    legacy_s16 signedWord = TRANSLATION_STORAGE_INITIAL;
    legacy_u32 dword = (legacy_u32)TRANSLATION_STORAGE_INITIAL;
    legacy_s32 signedDword = TRANSLATION_STORAGE_INITIAL;
    legacy_u8 *bytePointer = &byte;
    legacy_s16 *wordPointer = &signedWord;
    legacy_u32 *dwordPointer = &dword;
    *bytePointer = *bytePointer + TRANSLATION_STORAGE_STEP;
    signedByte = signedByte + TRANSLATION_STORAGE_STEP;
    word = word + TRANSLATION_STORAGE_STEP;
    *wordPointer = *wordPointer + TRANSLATION_STORAGE_STEP;
    *dwordPointer = *dwordPointer + TRANSLATION_STORAGE_STEP;
    signedDword = signedDword + TRANSLATION_STORAGE_STEP;
    return byte == TRANSLATION_STORAGE_EXPECTED && signedByte == TRANSLATION_STORAGE_EXPECTED &&
        word == TRANSLATION_STORAGE_EXPECTED && signedWord == TRANSLATION_STORAGE_EXPECTED &&
        dword == TRANSLATION_STORAGE_EXPECTED && signedDword == TRANSLATION_STORAGE_EXPECTED;
}
static legacy_s32 TranslationVariadic(legacy_s32 value, ...)
{
    va_list arguments;
    va_start(arguments, value);
    legacy_s32 result = value + va_arg(arguments, legacy_s32);
    va_end(arguments);
    return result;
}
static legacy_s32 TranslationIgnoredVariadic(legacy_s32 value, ...)
{
    return value;
}
static legacy_s32 TranslationPointerArray(legacy_s8 *arguments[])
{
    legacy_s16 index = 1;
    if (arguments[index][0] != '/' || arguments[index][1] != 'n' ||
            arguments[index][2] != 's' || arguments[index][3] != 0) { return 0; }
    index = 0;
    if (arguments[index++][1] != '-' || index != 1) { return 0; }
    switch (arguments[index][1]) {
    case 'n': return arguments[0][2] == 'f';
    default: return 0;
    }
}
static legacy_s32 TranslationScalarMatrix(legacy_s16 values[][3])
{
    legacy_s16 *row = values[1];
    return values[0][1] == 2 && values[1][2] == 6 && row[0] == 4;
}
static legacy_s32 TranslationPointerMatrix(legacy_s8 *values[][2])
{
    return values[0][1][1] == 'n' && values[1][0][2] == 'f' &&
        values[1][1][0] == '/';
}
legacy_s32 TranslationSemantics(void)
{
    legacy_u16 word = 65535U;
    legacy_s16 signedWord = 32767;
    legacy_u32 dword = 4294967295U;
    legacy_s32 sign = (legacy_s32)2147483648U;
    legacy_s32 side_effects = 0;
    legacy_s32 selected;
    legacy_u16 values[2] = {65535U, 5};
    legacy_s16 index = 0;
    struct TranslationPair source = {17, -23};
    struct TranslationPair destination;
    struct TranslationUnion overlay;
    if (translation_zero || translation_zero_words[6] || translation_partial[3]) { return 1; }
    if ((legacy_u16)65536U != 0 || (legacy_s16)65535U != -1) { return 2; }
    if ((legacy_s32)4294967295U != -1) { return 3; }
    if (word++ != 65535U || word != 0) { return 4; }
    if (signedWord++ != 32767 || signedWord != -32768) { return 5; }
    word -= 1;
    dword += 2;
    if (word != 65535U || dword != 1) { return 6; }
    if ((sign >> 1) != -1073741824) { return 7; }
    if (TranslationNarrow(65535) != -1 || TranslationArg(65535) != -1) { return 8; }
    selected = side_effects ? ++side_effects : (1 ? 42 : ++side_effects);
    if (selected != 42 || side_effects != 0) { return 9; }
    if (0 && values[side_effects++]++) { return 10; }
    if (!(1 || values[side_effects++]++)) { return 11; }
    if (side_effects != 0 || values[0] != 65535U) { return 12; }
    values[index++] += 2;
    if (index != 1 || values[0] != 1) { return 13; }
    destination = source;
    if (destination.first != 17 || destination.second != -23) { return 14; }
    if (translation_callbacks[0](9) != 10 || translation_callbacks[1](9) != 8) { return 15; }
    for (legacy_s16 outer = 0; outer < 3; outer++) {
        for (legacy_s16 inner = 0; inner < 3; inner++) {
            if (inner == 1) { continue; }
            side_effects++;
        }
    }
    if (side_effects != 6) { return 16; }
    if ((legacy_s32)-1 != (legacy_u32)4294967295U) { return 17; }
    if (((legacy_s32)-2 / (legacy_u32)2) != 2147483647U) { return 18; }
    if ((legacy_s16)-1 != (legacy_u32)4294967295U) { return 19; }
    destination = 1 ? translation_pairs[1] : source;
    if (destination.first != 3 || destination.second != -4) { return 20; }
    if ((legacy_s16)-3 / 2 != -1) { return 21; }
    selected = -1;
    switch (selected) {
    case -1: selected = 7; break;
    case (legacy_s16)65534U: return 22;
    default: return 23;
    }
    if (selected != 7) { return 24; }
    if (TranslationPointer()->second != -4 || (translation_pairs + 1)->first != 3) { return 25; }
    destination = TranslationResult(-31);
    if (destination.first != 7 || destination.second != -31) { return 26; }
    destination = 1 ? TranslationResult(-32) : TranslationResult(++side_effects);
    if (destination.second != -32 || side_effects != 6) { return 27; }
    destination = TranslationArgument(TranslationResult(-17));
    if (destination.first != 8 || destination.second != -18) { return 28; }
    destination = TranslationArgument(source);
    if (source.first != 17 || source.second != -23 || destination.first != 18) { return 29; }
    selected = 1 ? (&values[1] - &values[0]) + 1 : 0;
    if (selected != 2 || sizeof(values[0]) + 1 != 3) { return 30; }
    overlay.unsigned_value = 4294967295U;
    if (overlay.unsigned_value + 1U != 0U || overlay.signed_value != -1) { return 31; }
    selected = -7;
    selected /= 2;
    if (selected != -3 || -7 % 4 != -3 || (legacy_s16)-7 % 4 != -3) { return 32; }
    selected = -7;
    selected %= 4;
    if (selected != -3 || (legacy_s16)-7 / -2 != 3) { return 33; }
    if (translation_pairs->first != 1) { return 34; }
    signedWord = -1;
    signedWord++;
    if (signedWord != 0) { return 35; }
    signedWord = -2;
    signedWord += 3;
    if (signedWord != 1) { return 36; }
    sign = -1;
    sign++;
    if (sign != 0) { return 37; }
    if (TranslationNegative != 4294967295U) { return 38; }
    signedWord = TranslationByteResult();
    if (signedWord != -1 || TranslationByteResult() != -1) { return 39; }
    translation_pairs->second = -1;
    if (translation_pairs->second != 4294967295U) { return 40; }
    selected = 1 || TranslationEffect();
    if (selected != 1 || translation_effects != 0) { return 41; }
    selected = 0 && TranslationEffect();
    if (selected != 0 || translation_effects != 0) { return 42; }
    if (TranslationShortReturn() != 1 || translation_effects != 0) { return 43; }
    if (TranslationArg(1 || TranslationEffect()) != 1 || translation_effects != 0) { return 44; }
    selected = 0 || TranslationEffect();
    if (selected != 1 || translation_effects != 1) { return 45; }
    selected = 1 && TranslationEffect();
    if (selected != 1 || translation_effects != 2) { return 46; }
    if ((legacy_u64)translationPacked.value != 8) { return 47; }
    translationPacked.value = -1;
    if ((legacy_u64)translationPacked.value != 18446744073709551615ULL) { return 48; }
    translationPacked.doubleWord = 4294967295U;
    if ((legacy_s64)(legacy_s32)translationPacked.doubleWord != -1) { return 49; }
    translationPacked.value = 8;
    word = 0;
    word += translationPacked.value * sizeof(struct TranslationPair) + 6U;
    if (word != 38) { return 50; }
    if (!TranslationNarrowStorage()) { return TRANSLATION_STORAGE_FAILURE; }
    if (TranslationVariadic(TRANSLATION_STORAGE_INITIAL, TRANSLATION_STORAGE_STEP) !=
            TRANSLATION_STORAGE_EXPECTED ||
        TranslationIgnoredVariadic(TRANSLATION_STORAGE_EXPECTED, TRANSLATION_STORAGE_STEP) !=
            TRANSLATION_STORAGE_EXPECTED) { return TRANSLATION_VARIADIC_FAILURE; }
    legacy_f64 floating = 30 * 65536.0;
    if (floating != 1966080.0 || 2 + 0.5 != 2.5 || 2 / 4.0 != 0.5) { return 53; }
    if ((legacy_u16)3.9 != 3 || (legacy_s32)-3.9 != -3) { return 54; }
    if ((legacy_s64)-3.9 != -3 || (legacy_u64)3.9 != 3) { return 55; }
    floating = (legacy_f64)floating;
    floating += 2;
    if (floating != 1966082.0) { return 56; }
    legacy_f64 implicit_float = 2;
    if (implicit_float != 2.0) { return 57; }
    implicit_float = word;
    if (implicit_float != 38.0) { return 58; }
    legacy_f64 floating_zero = 0.0;
    legacy_f64 floating_positive = 6400.0;
    if ((floating_zero < floating_positive && 1) != 1) { return 59; }
    if ((floating_zero > floating_positive || 1) != 1) { return 60; }
    legacy_s32 floating_truth = floating_zero < floating_positive;
    if (floating_truth != 1 || !!(floating_zero < floating_positive) != 1) { return 61; }
    if (!floating_zero != 1 || !floating_positive != 0) { return 62; }
    legacy_s8 *arguments[2] = {"--fps:on", "/ns"};
    if (!TranslationPointerArray(arguments)) { return 63; }
    legacy_s16 matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
    if (!TranslationScalarMatrix(matrix)) { return 64; }
    legacy_s8 *pointer_matrix[2][2] = {{"--fps:on", "/ns"}, {"--fps:on", "/ns"}};
    if (!TranslationPointerMatrix(pointer_matrix)) { return 65; }
    return 0;
}
'''


def build(output, formatter=None):
    with tempfile.TemporaryDirectory(prefix="restunts-translation-tests-") as directory:
        directory = Path(directory)
        source = directory / "semantics.c"
        source.write_text(FIXTURE)
        host = directory / "host.c"
        host.write_text(FIXTURE + "\nlegacy_int main(void) { return TranslationSemantics(); }\n")
        subprocess.run(["gcc", "-std=c99", "-O2", "-I" + str(TRANSLATE.C_DIR), str(host), "-o", str(directory / "oracle")], check=True)
        subprocess.run([str(directory / "oracle")], check=True)
        headers = directory / "include"
        headers.mkdir()
        for name, text in TRANSLATE.FAKE_HEADERS.items():
            (headers / name).write_text(text)
        preprocessed = subprocess.run(["gcc", "-E", "-nostdinc", "-I" + str(headers), "-I" + str(TRANSLATE.C_DIR), str(source)], capture_output=True, text=True, check=True).stdout
        ast = TRANSLATE.c_parser.CParser().parse(preprocessed)
    model = TRANSLATE.Model()
    model.collect(ast, "semantics")
    name_map = TRANSLATE.build_name_map(model)
    generator = TRANSLATE.Generator(model)
    lines = ['#include "LEGACY.HH"', '#include "generated/CASTS.HC"']
    for name, value in model.enums.items():
        lines.append("#define " + name + " " + generator.expr(value)[1])
    for name, cls in model.classes.items():
        lines += [f"class {name}", "{"] + TRANSLATE.indent(generator.class_fields(cls)) + ["};"]
    declaration_position = len(lines)
    declarations = {name: "extern " + generator.prototype(decl.type, name) + ";"
                    for name, (decl, renames, definition) in model.functions.items() if definition}
    for name, (decl, renames) in model.global_defs.items():
        local = TRANSLATE.Generator(model, renames)
        lines += local.global_storage(decl, name)
    for module, ast, renames in model.units:
        for node in ast.ext:
            if isinstance(node, A.FuncDef):
                lines += TRANSLATE.Generator(model, renames).function(node, renames.get(node.decl.name, node.decl.name))
    lines += model.data_initializers
    lines += ['legacy_s32 translation_result = TranslationSemantics();',
              'Print("Translation semantics result: %d\\n", translation_result);',
              'if (translation_result) { throw(\'TestFail\'); }']
    needed = TRANSLATE.required_forwards({name_map.get(name, name) for name in declarations},
        TRANSLATE.STYLE.rename_identifiers("\n".join(lines), name_map))
    lines[declaration_position:declaration_position] = [declaration
        for name, declaration in declarations.items() if name_map.get(name, name) in needed]
    text = TRANSLATE.output_text(lines, output.name, name_map, formatter)
    output.write_bytes(text.replace("\r\n", "\n").replace("\n", "\r\n").encode())
    print("Host C oracle passed; wrote", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--formatter", help="Path to clang-format 18.1.8")
    args = parser.parse_args()
    build(args.output, args.formatter)
