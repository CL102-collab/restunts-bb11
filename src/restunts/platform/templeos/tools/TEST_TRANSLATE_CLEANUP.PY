#!/usr/bin/env python3
"""Check generated constant dependencies and native function replacements."""
import importlib.machinery
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
LOADER = importlib.machinery.SourceFileLoader("temple_translate_cleanup", str(HERE / "TRANSLATE.PY"))
SPEC = importlib.util.spec_from_loader(LOADER.name, LOADER)
TRANSLATE = importlib.util.module_from_spec(SPEC)
LOADER.exec_module(TRANSLATE)


class CleanupTests(unittest.TestCase):
    def test_module_filenames_preserve_existing_short_names(self):
        self.assertEqual(TRANSLATE.module_filename("frame"), "FRAME.HC")
        self.assertEqual(TRANSLATE.module_filename("hires_frame_visibility"),
                         "HIRES_FRAME_VISIBILITY.HC")

    def test_long_module_filenames_fit_stock_fat_without_losing_identity(self):
        modules = ["menu_track_editor_render", "menu_track_editor_input",
                   "extended_physics_collision", "hires_track_preview_scene",
                   "hires_track_preview_mode"]
        filenames = TRANSLATE.module_filenames(modules)
        self.assertEqual(set(filenames), set(modules))
        self.assertEqual(len(set(filenames.values())), len(modules))
        for module, filename in filenames.items():
            with self.subTest(module=module):
                self.assertLessEqual(len(filename), TRANSLATE.MODULE_FILENAME_LIMIT)
                self.assertTrue(filename.isascii())
                self.assertEqual(filename, TRANSLATE.module_filename(module))
                self.assertTrue(filename.endswith(".HC"))
        self.assertNotEqual(filenames["hires_track_preview_scene"],
                            filenames["hires_track_preview_mode"])

    def test_module_filename_collisions_fail_before_output_is_overwritten(self):
        with patch.object(TRANSLATE, "module_filename", return_value="COLLISION.HC"):
            with self.assertRaisesRegex(ValueError, "filename collision"):
                TRANSLATE.module_filenames(["first", "second"])

    def test_translation_rejects_filename_collisions_before_any_output(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(TRANSLATE, "prepare_sources", return_value=Path(directory)), \
                    patch.object(TRANSLATE, "source_names", return_value=["first", "second"]), \
                    patch.object(TRANSLATE.subprocess, "run", return_value=
                                 subprocess.CompletedProcess([], 0, stdout="", stderr="")), \
                    patch.object(TRANSLATE, "module_filename", return_value="COLLISION.HC"), \
                    patch.object(TRANSLATE, "write") as write, patch("builtins.print"):
                with self.assertRaisesRegex(ValueError, "filename collision"):
                    TRANSLATE.translate("cpp")
                write.assert_not_called()

    def test_obsolete_module_cleanup_preserves_unrelated_generated_files(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            old = output / "MENU_TRACK_EDITOR_RENDER.HC"
            current = output / TRANSLATE.module_filename("menu_track_editor_render")
            unrelated = output / "UNRELATED_LONG_GENERATED_MODULE.HC"
            for path in (old, current, unrelated):
                path.write_text("generated")
            with patch.object(TRANSLATE, "GEN", output), \
                    patch.object(TRANSLATE, "CHECK", False):
                TRANSLATE.remove_obsolete_module_output("menu_track_editor_render", current.name)
            self.assertFalse(old.exists())
            self.assertTrue(current.exists())
            self.assertTrue(unrelated.exists())

    def test_obsolete_module_check_reports_without_deleting(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            old = output / "MENU_TRACK_EDITOR_RENDER.HC"
            old.write_text("generated")
            with patch.object(TRANSLATE, "GEN", output), \
                    patch.object(TRANSLATE, "CHECK", True), \
                    patch.object(TRANSLATE, "MISMATCHES", []):
                TRANSLATE.remove_obsolete_module_output(
                    "menu_track_editor_render", TRANSLATE.module_filename("menu_track_editor_render"))
                self.assertEqual(TRANSLATE.MISMATCHES,
                                 [old.name + " (obsolete module output)"])
            self.assertTrue(old.exists())

    def test_metadata_check_accepts_git_checkout_line_endings(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "MANIFEST.JSON").write_bytes(b'{"modules": []}\n')
            with patch.object(TRANSLATE, "GEN", output), \
                    patch.object(TRANSLATE, "CHECK", True), \
                    patch.object(TRANSLATE, "MISMATCHES", []):
                TRANSLATE.write("MANIFEST.JSON", ['{"modules": []}'])
                self.assertEqual(TRANSLATE.MISMATCHES, [])

    def test_native_background_storage_compiles_with_flat_pointer_contexts(self):
        with tempfile.TemporaryDirectory() as directory:
            sources = TRANSLATE.prepare_sources(Path(directory))
            result = subprocess.run([
                "gcc", "-fsyntax-only", "-Wno-implicit-function-declaration",
                "-DRESTUNTS_FULL", "-DRESTUNTS_TEMPLEOS",
                str(sources / "shape2d/shape2d_window.c"),
            ], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_nested_source_paths_keep_flat_module_names(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            feed = root / "src/restunts/dos/full-game-objects.mk"
            feed.parent.mkdir(parents=True)
            feed.write_text("RESTUNTS_OBJ_FILES = shape2d/shape2d.obj "
                            "extended/extended_trackobj.obj shape2d/shape2d.obj\n")
            with patch.object(TRANSLATE, "ROOT", root):
                paths = TRANSLATE.source_names()
            self.assertEqual(paths, ["shape2d/shape2d", "extended/extended_trackobj", "stateio"])
            self.assertEqual([Path(name).name for name in paths],
                             ["shape2d", "extended_trackobj", "stateio"])

    def test_relocated_memory_helpers_use_native_raster_service(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            sources = TRANSLATE.prepare_sources(directory)
            headers = directory / "include"
            headers.mkdir()
            for name, text in TRANSLATE.FAKE_HEADERS.items():
                (headers / name).write_text(text)
            source = directory / "memory-helper.c"
            source.write_text('#include "hires/memory.h"\n')
            preprocessed = subprocess.run([
                "gcc", "-E", "-nostdinc", "-I" + str(headers), "-I" + str(sources),
                "-DRESTUNTS_FULL", "-DRESTUNTS_TEMPLEOS", str(source),
            ], text=True, capture_output=True, check=True).stdout
            tree = TRANSLATE.c_parser.CParser().parse(preprocessed)
        model = TRANSLATE.Model()
        model.collect(tree, "memory_helper")
        TRANSLATE.build_name_map(model)
        helper = next(node for node in tree.ext if isinstance(node, TRANSLATE.A.FuncDef)
                      and node.decl.name == "hires_forget_paragraphs")
        generator = TRANSLATE.Generator(model)
        generator.function(helper, "hires_forget_paragraphs")
        self.assertIn("TempleRasterForgetRange", model.external_used)
        self.assertNotIn("hires_forget_range", model.external_used)
        self.assertTrue(set(model.classes).isdisjoint({
            "HIRES_DEPTH_PLANE", "HIRES_RASTER_TARGET", "HIRES_RASTER_CONTEXT",
        }), "SDL companion raster types must stay outside the native model")

    def test_prepared_sources_preserve_nested_header_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            sources = TRANSLATE.prepare_sources(Path(directory))
            self.assertTrue((sources / "shape2d/shape2d_blit.c").is_file())
            self.assertTrue((sources / "shape2d/shape2d_internal.h").is_file())
            self.assertTrue((sources / "extended/extended_track.h").is_file())
            self.assertFalse((sources / "shape2d_blit.c").exists())

    def test_constants_follow_transitive_dependencies(self):
        definitions = {"BASE": "2", "NEXT": "BASE + 1", "LIMIT": "NEXT + 1",
                       "UNUSED": "BASE + 9"}
        self.assertEqual(TRANSLATE.referenced_defines(definitions, ["values[LIMIT];"]),
                         {"BASE", "NEXT", "LIMIT"})

    def test_native_only_constants_and_cycles(self):
        definitions = {"NATIVE": "FIRST", "FIRST": "SECOND", "SECOND": "FIRST",
                       "UNUSED": "0"}
        self.assertEqual(TRANSLATE.referenced_defines(definitions,
                         ["legacy_s64 generated;", "Check(NATIVE);"]),
                         {"NATIVE", "FIRST", "SECOND"})

    def test_comments_strings_and_unused_dependencies_do_not_keep_constants(self):
        definitions = {"COMMENT": "0", "STRING": "0", "UNUSED": "DEPENDENCY",
                       "DEPENDENCY": "0"}
        self.assertEqual(TRANSLATE.referenced_defines(definitions,
                         ['// COMMENT\n/* COMMENT */ Print("STRING");']), set())
        self.assertEqual(TRANSLATE.referenced_defines(definitions, []), set())

    def test_function_forwards_follow_first_use(self):
        source = """
            legacy_void First() { Later(); }
            legacy_void Later() { First(); Later(); }
            legacy_void Last() { Later(); }
        """
        self.assertEqual(TRANSLATE.required_forwards({"First", "Later", "Last"}, source),
                         {"Later"})

    def test_function_addresses_and_global_initializers_need_forwards(self):
        source = """
            legacy_u8 *callback = &Handler;
            legacy_s64 *pointer = &value;
            legacy_void Handler() {}
            legacy_s64 value;
        """
        self.assertEqual(TRANSLATE.required_forwards({"callback", "Handler", "pointer", "value"},
                         source), {"Handler", "value"})

    def test_native_extern_prevents_a_duplicate_generated_header(self):
        source = """
            extern legacy_void Later();
            legacy_void First() { Later(); }
            legacy_void Later() {}
        """
        self.assertEqual(TRANSLATE.required_forwards({"First", "Later"}, source), set())

    def test_class_cycles_and_self_pointers(self):
        source = """
            class CFirst { CFirst *self; CSecond *next; };
            class CSecond { CFirst *previous; };
        """
        self.assertEqual(TRANSLATE.required_forwards({"CFirst", "CSecond"}, source), {"CSecond"})

    def test_local_shadows_and_member_names_are_not_global_references(self):
        source = """
            class CNode { legacy_s64 value; };
            legacy_void First(legacy_s64 value) {
                legacy_s64 noreg local, noreg other;
                CNode *node;
                local = value + node->value + other;
            }
            legacy_s64 value, local, other;
        """
        self.assertEqual(TRANSLATE.required_forwards({"value", "local", "other"}, source), set())

    def test_prior_global_use_is_not_hidden_by_a_later_local(self):
        source = """
            legacy_void First() { value = 1; legacy_s64 value; value = 2; }
            legacy_s64 value;
        """
        self.assertEqual(TRANSLATE.required_forwards({"value"}, source), {"value"})

    def test_nested_local_remains_in_holyc_function_scope(self):
        source = """
            legacy_void First() { { legacy_s64 value; value = 1; } value = 2; }
            legacy_s64 value;
        """
        self.assertEqual(TRANSLATE.required_forwards({"value"}, source), set())

    def test_function_pointer_declarators_and_casts(self):
        source = """
            legacy_void (*callback)() = Handler;
            legacy_void Handler() {}
            legacy_void First(legacy_void (*value)()) {
                callback();
                value();
                callback(legacy_u8 *);
            }
            legacy_u8 *value;
        """
        self.assertEqual(TRANSLATE.required_forwards({"callback", "Handler", "First", "value"},
                         source), {"Handler"})

    def test_include_order_uses_buffered_root_relative_generated_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = {root / "LOAD.HC": '#include "generated/ALL.HC"\nAfter();',
                       root / "generated/ALL.HC": '#include "generated/FUN.HC"\nMiddle();',
                       root / "generated/FUN.HC": 'Before();'}
            with patch.object(TRANSLATE, "HERE", root):
                self.assertEqual(TRANSLATE.compilation_source(root / "LOAD.HC", sources),
                                 'Before();\nMiddle();\nAfter();')

    def test_raster_replacement_does_not_generate_discarded_locals(self):
        tree = TRANSLATE.c_parser.CParser().parse("""
            typedef unsigned char legacy_u8;
            typedef unsigned short legacy_u16;
            typedef short legacy_s16;
            typedef void legacy_void;
            legacy_void shape2d_write_raster(legacy_u8 *bitmap, legacy_u16 destination,
                legacy_u8 value, legacy_s16 operation, legacy_u8 planar)
            {
                legacy_u8 unused = planar ? value : bitmap[destination];
                bitmap[destination] = unused;
            }
        """)
        model = TRANSLATE.Model()
        model.collect(tree, "fixture")
        TRANSLATE.build_name_map(model)
        generator = TRANSLATE.Generator(model)
        body = generator.function(tree.ext[-1], "shape2d_write_raster")
        self.assertEqual(generator.local_decls, [])
        self.assertIn("\tplanar;", body)
        self.assertIn("\tvideo_pages_raster_span(bitmap, destination, &value, 0, 1, operation, 0);", body)


    def test_bitmap_context_wraps_each_draw_and_cleans_up_early_returns(self):
        for name in ("shape2d_render_rle", "shape2d_render_rle_clipped",
                     "sprite_putimage_at", "sprite_shape_to_1_impl"):
            with self.subTest(function=name):
                tree = TRANSLATE.c_parser.CParser().parse("""
                    typedef unsigned char legacy_u8;
                    typedef unsigned short legacy_u16;
                    typedef void legacy_void;
                    legacy_void %s(legacy_u8 *shape, legacy_u16 x, legacy_u16 y)
                    {
                        if (!shape) { return; }
                        if (x) { return; }
                        shape[y] = 1;
                    }
                """ % name)
                model = TRANSLATE.Model()
                model.collect(tree, "fixture")
                TRANSLATE.build_name_map(model)
                generator = TRANSLATE.Generator(model)
                body = generator.function(tree.ext[-1], name)
                statements = [line.strip() for line in body if line.strip()]
                self.assertEqual(statements.count("TempleRasterSourceAt(shape, y);"), 1)
                self.assertLess(statements.index("TempleRasterSourceAt(shape, y);"),
                                next(index for index, line in enumerate(statements)
                                     if line.startswith("if (")))
                self.assertEqual(statements.count("TempleRasterSource(0);"), 3)
                for index, statement in enumerate(statements):
                    if statement == "return;":
                        self.assertEqual(statements[index - 1], "TempleRasterSource(0);")
                self.assertEqual(statements[-2], "TempleRasterSource(0);")
                self.assertTrue({"TempleRasterSource", "TempleRasterSourceAt"}
                                <= model.external_used)


if __name__ == "__main__":
    unittest.main()
