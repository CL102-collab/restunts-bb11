#!/usr/bin/env python3
"""Check C integer widths and contiguous-array pointer declarations."""
import importlib.machinery
import importlib.util
import itertools
from pathlib import Path
import sys
import unittest

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
LOADER = importlib.machinery.SourceFileLoader("temple_translate_types", str(HERE / "TRANSLATE.PY"))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
TRANSLATE = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(TRANSLATE)


class BuiltinTypeTests(unittest.TestCase):
    def setUp(self):
        self.parser = TRANSLATE.c_parser.CParser()
        self.generator = TRANSLATE.Generator(TRANSLATE.Model())

    def type_node(self, spelling):
        return self.parser.parse(spelling + " value;", filename="types.c").ext[0].type

    def test_integer_keyword_permutations(self):
        cases = {
            "signed": ("legacy_int", (32, True)),
            "unsigned": ("legacy_uint", (32, False)),
            "signed int": ("legacy_int", (32, True)),
            "unsigned int": ("legacy_uint", (32, False)),
            "signed short": ("legacy_s16", (16, True)),
            "unsigned short": ("legacy_u16", (16, False)),
            "short int": ("legacy_s16", (16, True)),
            "signed short int": ("legacy_s16", (16, True)),
            "unsigned short int": ("legacy_u16", (16, False)),
            "signed long": ("legacy_s64", (64, True)),
            "unsigned long": ("legacy_u64", (64, False)),
            "long int": ("legacy_s64", (64, True)),
            "signed long int": ("legacy_s64", (64, True)),
            "unsigned long int": ("legacy_u64", (64, False)),
            "signed long long": ("legacy_s64", (64, True)),
            "unsigned long long": ("legacy_u64", (64, False)),
            "long long int": ("legacy_s64", (64, True)),
            "signed long long int": ("legacy_s64", (64, True)),
            "unsigned long long int": ("legacy_u64", (64, False)),
            "signed char": ("legacy_s8", (8, True)),
            "unsigned char": ("legacy_u8", (8, False)),
        }
        for canonical, (expected, info) in cases.items():
            for words in sorted(set(itertools.permutations(canonical.split()))):
                spelling = " ".join(words)
                with self.subTest(spelling=spelling):
                    node = self.type_node(spelling)
                    self.assertEqual(self.generator.type_text(node), expected)
                    self.assertEqual(self.generator.integer_info(node), info)

    def test_unsigned_cast_uses_value_width_helper(self):
        self.generator.in_function = True
        for spelling, helper in (("unsigned", "TempleCastU32"),
                                 ("signed", "TempleCastS32"),
                                 ("short unsigned int", "TempleCastU16"),
                                 ("long unsigned int", "TempleCastU64")):
            with self.subTest(spelling=spelling):
                ast = self.parser.parse("void Check(void) { (" + spelling + ")-1; }")
                _, text = self.generator.expr(ast.ext[0].body.block_items[0])
                self.assertTrue(text.startswith(helper + "("), text)
                self.assertNotIn("(unsigned)", text)
                self.assertNotIn("(signed)", text)

    def test_unsigned_local_keeps_32_bit_storage(self):
        text = self.generator.local_decl(self.type_node("unsigned"), "value")
        self.assertEqual(text, "legacy_uint noreg value")

    def test_alias_resolves_normalized_builtin(self):
        ast = self.parser.parse("typedef unsigned Count; Count value;", filename="types.c")
        model = TRANSLATE.Model()
        model.collect(ast, "types")
        generator = TRANSLATE.Generator(model)
        self.assertEqual(generator.type_text(ast.ext[1].type), "legacy_uint")
        self.assertEqual(generator.integer_info(ast.ext[1].type), (32, False))

    def test_existing_scalar_types_stay_unchanged(self):
        for spelling, expected in (("char", "legacy_char"), ("float", "legacy_f64"),
                                   ("double", "legacy_f64"), ("void", "legacy_void")):
            with self.subTest(spelling=spelling):
                self.assertEqual(self.generator.type_text(self.type_node(spelling)), expected)

    def test_array_typenames_retain_sizeof_dimensions(self):
        cases = (("char[12]", "(12 * sizeof(legacy_char))"),
                 ("short[2][3]", "(2 * 3 * sizeof(legacy_s16))"),
                 ("char *[3]", "(3 * sizeof(legacy_char *))"))
        for spelling, expected in cases:
            with self.subTest(spelling=spelling):
                ast = self.parser.parse("void Check(void) { sizeof(" + spelling + "); }")
                prelude, text = self.generator.expr(ast.ext[0].body.block_items[0])
                self.assertEqual(prelude, [])
                self.assertEqual(text, expected)

    def test_unsized_extern_keeps_inferred_array_size(self):
        definition = self.parser.parse(
            "extern const signed char thresholds[]; "
            "const signed char thresholds[] = {99, 20, 18, 16, 14, 12, 10}; "
            "void Check(void) { sizeof(thresholds); }", filename="scene.c")
        declaration = self.parser.parse("extern const signed char thresholds[];",
                                        filename="renderer.c")
        model = TRANSLATE.Model()
        model.collect(definition, "scene")
        model.collect(declaration, "renderer")
        generator = TRANSLATE.Generator(model)
        prelude, text = generator.expr(definition.ext[-1].body.block_items[0])
        self.assertEqual(prelude, [])
        self.assertEqual(text, "(7 * sizeof(legacy_s8))")


    def test_array_pointer_uses_contiguous_element_storage(self):
        for source, expected in (("short (*value)[3];", "legacy_s16 *"),
                                 ("short (*value)[2][3];", "legacy_s16 *"),
                                 ("char *(*value)[2];", "legacy_char * *")):
            with self.subTest(source=source):
                node = self.parser.parse(source).ext[0].type
                self.assertEqual(self.generator.type_text(node), expected)
                self.assertEqual(self.generator.decl(node, "value").replace(" ", ""),
                                 (expected + "value").replace(" ", ""))

    def test_class_array_pointer_retains_class_element_type(self):
        ast = self.parser.parse("struct Pair { short value; }; struct Pair (*rows)[6];")
        node = ast.ext[1].type
        self.assertEqual(self.generator.type_text(node), "Pair *")
        self.assertEqual(self.generator.decl(node, "rows"), "Pair *rows")

    def test_array_of_row_pointers_keeps_outer_storage(self):
        node = self.parser.parse("short (*rows[2])[3];").ext[0].type
        self.assertEqual(self.generator.decl(node, "rows"), "legacy_s16 *rows[2]")

    def test_array_alias_retains_rows_and_adjusts_parameters(self):
        ast = self.parser.parse("typedef short Row[3]; Row *rows; void Check(Row values[2]);")
        model = TRANSLATE.Model()
        model.collect(ast, "types")
        generator = TRANSLATE.Generator(model)
        self.assertEqual(generator.decl(ast.ext[1].type, "rows"), "legacy_s16 *rows")
        self.assertEqual(generator.prototype(ast.ext[2].type, "Check"),
                         "legacy_void Check(legacy_s16 *values)")


if __name__ == "__main__":
    unittest.main()
