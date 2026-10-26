#!/bin/sh
# Keep the complete runtime beside this Finder entry point.
set -eu
bundle_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$bundle_directory/../../../bin/genghost-gui" "$@"
