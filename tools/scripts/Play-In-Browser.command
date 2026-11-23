#!/bin/bash

if ! python3 -c 'from http.server import ThreadingHTTPServer' >/dev/null 2>&1; then
    echo 'Install Python 3.7 or later for macOS and make python3 available in Terminal.' >&2
    echo 'https://www.python.org/downloads/macos/' >&2
    exit 1
fi

exec python3 "$(dirname -- "${BASH_SOURCE[0]}")/serve-browser-game.py" --open-browser "$@"
