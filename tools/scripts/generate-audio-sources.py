#!/usr/bin/env python3
"""Embed the independent browser audio runtime and playback sources for emcc."""

import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--worklet", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sources = {name: getattr(args, name).read_text(encoding="utf-8")
               for name in ("runtime", "worker", "worklet")}
    # The resulting prelude eventually lives inside an HTML script element.
    # Escape markup even when it occurs inside a JavaScript string literal.
    encoded = json.dumps(sources, ensure_ascii=True, separators=(",", ":"))
    encoded = encoded.replace("<", "\\u003c").replace(">", "\\u003e")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("Module['restuntsAudioSources']=" + encoded + ";\n", encoding="utf-8")


if __name__ == "__main__":
    main()
