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
    if (JSON.stringify(loaded.snapshot()) !== JSON.stringify(doc.snapshot())) {
        throw new Error('Embedded codec changed the saved track');
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
