"""Check the standalone editor's assets, execution order and offline codec."""

import importlib.util
from html.parser import HTMLParser
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "build-track-editor.py"
SPEC = importlib.util.spec_from_file_location("build_track_editor", SCRIPT)
EDITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EDITOR)


class BundleContents(HTMLParser):
    def __init__(self, source):
        super().__init__()
        self.resource_references = []
        self.scripts = []
        self.styles = []
        self.current = None
        self.canvas_seen = False
        self.feed(source)

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if "src" in attributes or (tag == "link" and "href" in attributes):
            self.resource_references.append(attributes)
        if tag == "canvas":
            self.canvas_seen = True
        if tag == "script":
            if not self.canvas_seen:
                raise AssertionError("Inline scripts ran before the editor DOM was parsed")
            self.scripts.append("")
            self.current = self.scripts
        elif tag == "style":
            self.styles.append("")
            self.current = self.styles

    def handle_data(self, data):
        if self.current is not None:
            self.current[-1] += data

    def handle_endtag(self, tag):
        if tag in ("script", "style"):
            self.current = None


class BuildTrackEditorTests(unittest.TestCase):
    def test_all_editor_dependencies_are_embedded_in_execution_order(self):
        contents = BundleContents(EDITOR.build_editor())
        self.assertEqual(contents.resource_references, [])
        self.assertEqual(len(contents.styles), 1)
        self.assertIn(".editor-layout", contents.styles[0])
        names = ("model", "resources", "route-data", "validation", None)
        self.assertEqual(len(contents.scripts), len(names))
        for script, name in zip(contents.scripts, names):
            filename = f"track-editor-{name}.js" if name else "track-editor.js"
            expected = (EDITOR.SOURCE_DIRECTORY / filename).read_text(encoding="utf-8")
            self.assertEqual(script.strip(), expected.strip())

    def test_command_writes_only_the_requested_html_with_stable_crlf(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "package" / EDITOR.OUTPUT_NAME
            subprocess.run([sys.executable, str(SCRIPT), "--output", str(output)], check=True)
            first = output.read_bytes()
            self.assertNotIn(b"\n", first.replace(b"\r\n", b""))
            EDITOR.write_editor(output)
            self.assertEqual(output.read_bytes(), first)
            self.assertEqual(list(output.parent.iterdir()), [output])

    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for the offline codec check")
    def test_embedded_browser_modules_edit_save_reload_and_validate(self):
        scripts = BundleContents(EDITOR.build_editor()).scripts
        # No require(), filesystem or network are exposed to this browser-like
        # context. All route data and codecs must come from the embedded scripts.
        code = """
const vm = require('node:vm');
const scripts = JSON.parse(require('node:fs').readFileSync(0, 'utf8'));
const browser = vm.createContext({});
for (const script of scripts.slice(0, -1)) vm.runInContext(script, browser);
vm.runInContext(`
    const model = TrackEditorModel;
    const doc = new model.Document(12, 10);
    doc.setNotes('An offline track');
    doc.place(1, 3, 3, 0, 0);
    const loaded = model.decodeCstrk(model.encodeCstrk(doc));
    if (JSON.stringify(model.encodeCstrk(loaded)) !== JSON.stringify(model.encodeCstrk(doc))) {
        throw new Error('Embedded codec changed the saved track');
    }
    const combined = model.library.find(piece => piece.tunnelSlalom);
    if (!combined || combined.name !== 'Tunnel with slalom') throw new Error('Combined stunt missing from palette');
    const stunt = new model.Document(4, 4);
    const variant = model.pieceVariant(combined, 1);
    stunt.place(variant.piece.id, 1, 1, 2, variant.rotation, 0, variant.piece.tunnelSlalom);
    const reopened = model.decodeCstrk(model.encodeCstrk(stunt));
    if (reopened.version !== model.VERSION || reopened.objects[0].tunnelSlalom !== 1 || reopened.objects[0].surface !== 2) {
        throw new Error('Embedded codec lost the combined stunt');
    }
    const roll = model.library.find(piece => piece.rollRamp);
    if (!roll || roll.name !== 'Barrel roll ramp') throw new Error('Barrel roll ramp missing from palette');
    const ramp = new model.Document(4, 4), launch = model.pieceVariant(roll, 3);
    ramp.place(launch.piece.id, 1, 1, 1, launch.rotation, 0, 0, launch.piece.rollRamp);
    const rampReloaded = model.decodeCstrk(model.encodeCstrk(ramp));
    const rampDisplay = model.pieceDisplayVariant({ ...model.catalog.find(piece => piece.id === rampReloaded.objects[0].id), rollRamp: 1 }, rampReloaded.objects[0].rotation);
    if (rampReloaded.version !== 8 || rampReloaded.objects[0].rollRamp !== 1 || rampDisplay.rotation !== 3) {
        throw new Error('Embedded codec lost the barrel roll launch direction');
    }
    const rampIcon = TrackEditorResources.composePieceTileBitmap(new Map(), 85, 0, 1);
    if (rampIcon.width !== 15 || rampIcon.height !== 30) throw new Error('Embedded ramp artwork is invalid');
    for (const id of [109, 111, 154]) {
        const slope = new model.Document(4, 4);
        for (let y = 0; y <= slope.height; y++) for (let x = 0; x <= slope.width; x++) {
            slope.vertices[y * (slope.width + 1) + x] = Math.min(x, 2);
        }
        const object = new model.Document(4, 4).place(id, 1, 1);
        slope.objects.push({ ...object, baseHeight: 1, slopeX: 1 });
        const imported = model.decodeCstrk(model.encodeCstrk(slope));
        if (imported.validate().length) throw new Error('Imported slope placement is invalid');
        let blocked = false;
        try { imported.placement(id, 1, 2); }
        catch (error) { blocked = /level ground.*editor/.test(error.message); }
        if (!blocked) throw new Error('Embedded editor allows restricted slope placement');
        if (!imported.erase(1, 1) || imported.objects.length || !imported.undo()) {
            throw new Error('Imported slope placement cannot be removed and restored');
        }
        model.decodeCstrk(model.encodeCstrk(imported));
    }
    if (!TrackEditorResources || !TrackEditorRouteData || !TrackEditorValidation) {
        throw new Error('A browser module is missing');
    }
    const result = TrackEditorValidation.validateRoute(new model.Document(12, 10));
    if (result.status !== TrackEditorValidation.STATUS.NO_START_FINISH) {
        throw new Error('Embedded route validation did not run');
    }
`, browser);
"""
        subprocess.run(["node", "-e", code], input=json.dumps(scripts),
                       text=True, check=True, capture_output=True)

    def test_missing_dependency_does_not_write_partial_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source"
            source.mkdir()
            (source / "track-editor.html").write_text(
                '<body><script defer src="missing.js"></script></body>', encoding="utf-8")
            output = Path(temporary) / EDITOR.OUTPUT_NAME
            with self.assertRaises(FileNotFoundError):
                EDITOR.write_editor(output, source)
            self.assertFalse(output.exists())

    def test_embedded_script_cannot_prematurely_close_its_html_element(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary)
            (source / "track-editor.html").write_text(
                '<body><canvas></canvas><script src="example.js" defer></script></body>',
                encoding="utf-8")
            (source / "example.js").write_text('const text = "</script>";', encoding="utf-8")
            contents = BundleContents(EDITOR.build_editor(source))
            self.assertEqual(contents.scripts, ['\nconst text = "<\\/script>";\n'])


if __name__ == "__main__":
    unittest.main()
