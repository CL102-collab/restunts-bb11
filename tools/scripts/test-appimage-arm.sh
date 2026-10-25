#!/usr/bin/env bash
# Keep target library paths out of the host Python and QEMU processes.
set -euo pipefail
unset LD_LIBRARY_PATH LD_PRELOAD LIBRARY_PATH QEMU_SET_ENV QEMU_UNSET_ENV QEMU_LD_PREFIX
unset APPDIR APPIMAGE_EXTRACT_AND_RUN
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
exec python3 "$script_dir/test-appimage-arm.py" "$@"
