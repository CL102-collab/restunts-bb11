#!/bin/sh
# Verify and execute the exact cross-built archive in its target BSD VM.
set -eu

script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(cd -- "$script_dir/../.." && pwd)
target=${1:?Usage: test-bsd-cross-package.sh TARGET}
case "$target" in
    freebsd-arm64|freebsd-riscv64|openbsd-arm64|openbsd-riscv64|netbsd-arm64|netbsd-riscv64) ;;
    *) echo "Not a BSD cross target: $target" >&2; exit 1 ;;
esac
# This also rejects a mismatched OS/architecture before attempting execution.
sh "$script_dir/install-bsd-package-dependencies.sh" "$target"
python_command=${PYTHON:-python3.13}
"$python_command" "$script_dir/release-packages.py" verify --target "$target" \
    --directory "$repo_dir/dist/packages" --commit "${GITHUB_SHA:?Missing source commit}" \
    --run-id "${GITHUB_RUN_ID:?Missing workflow run}" \
    --run-attempt "${GITHUB_RUN_ATTEMPT:?Missing workflow attempt}"
runtime=$(mktemp -d "${TMPDIR:-/tmp}/restunts-cross-package.XXXXXX")
trap 'rm -rf -- "$runtime"' 0
trap 'exit 1' 1 2 15
tar -xzf "$repo_dir/dist/packages/restunts-$target.tar.gz" -C "$runtime"
sh "$script_dir/test-relocated-runtime.sh" "$runtime"
