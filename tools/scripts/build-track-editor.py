#!/usr/bin/env python3
"""Bundle the browser track editor into one offline tracked.html file."""

import argparse
from html.parser import HTMLParser
from pathlib import Path
import re
from urllib.parse import urlsplit


SOURCE_DIRECTORY = Path(__file__).resolve().parents[2] / "src/restunts/platform/wasm"
OUTPUT_NAME = "tracked.html"


class EditorSource(HTMLParser):
    def __init__(self, source, directory):
        super().__init__(convert_charrefs=False)
        self.source = source
        self.directory = Path(directory).resolve()
        self.line_offsets = [0]
        for line in source.splitlines(keepends=True):
            self.line_offsets.append(self.line_offsets[-1] + len(line))
        self.replacements = []
        self.scripts = []
        self.pending_script = None
        self.body_end = None

    def source_offset(self):
        line, column = self.getpos()
        return self.line_offsets[line - 1] + column

    def asset(self, name):
        url = urlsplit(name)
        if url.scheme or url.netloc or url.query or url.fragment:
            raise ValueError(f"Editor asset must be a local file: {name}")
        path = (self.directory / url.path).resolve()
        if not path.is_relative_to(self.directory):
            raise ValueError(f"Editor asset is outside the source directory: {name}")
        return path.read_text(encoding="utf-8")

    def handle_starttag(self, tag, attributes):
        attributes = dict(attributes)
        if tag == "link" and attributes.get("rel", "").lower() == "stylesheet":
            css = self.asset(attributes["href"])
            # Raw-text elements must not contain a literal HTML closing tag.
            css = re.sub(r"</style", r"<\\/style", css, flags=re.IGNORECASE)
            start = self.source_offset()
            self.replacements.append((start, start + len(self.get_starttag_text()),
                                      f"<style>\n{css.rstrip()}\n</style>"))
        elif tag == "script" and "src" in attributes:
            if "defer" not in attributes or "async" in attributes \
                    or attributes.get("type", "text/javascript") != "text/javascript":
                raise ValueError("Editor scripts must be deferred classic scripts")
            script = self.asset(attributes["src"])
            script = re.sub(r"</script", r"<\\/script", script, flags=re.IGNORECASE)
            self.scripts.append(f"<script>\n{script.rstrip()}\n</script>")
            self.pending_script = self.source_offset()

    def handle_endtag(self, tag):
        if tag == "script" and self.pending_script is not None:
            end = self.source.index(">", self.source_offset()) + 1
            self.replacements.append((self.pending_script, end, ""))
            self.pending_script = None
        elif tag == "body":
            self.body_end = self.source_offset()


def build_editor(source_directory=SOURCE_DIRECTORY):
    source_directory = Path(source_directory)
    source = (source_directory / "track-editor.html").read_text(encoding="utf-8")
    editor = EditorSource(source, source_directory)
    editor.feed(source)
    editor.close()
    if editor.body_end is None or not editor.scripts or editor.pending_script is not None:
        raise ValueError("Editor source must have a body and complete deferred scripts")
    # Inline defer does not defer execution. Move the scripts after the editor's
    # DOM, in their original order, so startup sees the same document as before.
    editor.replacements.append((editor.body_end, editor.body_end,
                                "\n".join(editor.scripts) + "\n"))
    for start, end, replacement in sorted(editor.replacements, reverse=True):
        source = source[:start] + replacement + source[end:]
    return source


def write_editor(output, source_directory=SOURCE_DIRECTORY):
    editor = build_editor(source_directory)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    # HTML is a DOS/Windows-facing format in .gitattributes.
    output.write_bytes(editor.replace("\n", "\r\n").encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path,
                        help="Destination for the standalone tracked.html")
    args = parser.parse_args()
    write_editor(args.output)


if __name__ == "__main__":
    main()
