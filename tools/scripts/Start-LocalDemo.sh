#!/usr/bin/env bash

if ! python3 -c 'from http.server import ThreadingHTTPServer' >/dev/null 2>&1; then
    echo "Install Python 3.7 or later using your Linux distribution's package manager." >&2
    exit 1
fi

exec python3 "$(dirname -- "${BASH_SOURCE[0]}")/serve-local-demo.py" --open-browser "$@"
