#!/usr/bin/env bash
# Install reproducible Linux x86-64 FS-UAE and its open AROS replacement ROM.
# The newer AROS ROM has a much smaller RAM footprint than FS-UAE's 2015 ROM.
set -euo pipefail

destination=${1:-/tmp/restunts-amiga-emulator}
fsuae_version=3.2.35
fsuae_sha=dbecec21bba0d66c4b3d1e280c7dba472511c1c04ff07c34757c46d596cd07ca
winuae_revision=24ccc6cac29eccd423134aa5838c0024708b479e
aros_source_sha=d729b08ee385c882b811bcd2e35c3c7262a6c89fbdbd41030be1fc748746cf8a
aros_rom_sha=ef631aea3f21571a15e63f2e7189b37a4eef48ca4b1b20ec61ab3115010bdda3

if [[ -e "$destination" ]]; then
    echo "Refusing to replace $destination; choose an empty destination." >&2
    exit 1
fi
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    echo "This emulator package requires Linux x86-64." >&2
    exit 1
fi
staging=$(mktemp -d)
trap 'rm -rf -- "$staging"' EXIT
curl --fail --location --retry 3 --output "$staging/fs-uae.tar.xz" \
    "https://github.com/FrodeSolheim/fs-uae/releases/download/v$fsuae_version/FS-UAE_${fsuae_version}_Linux_x86-64.tar.xz"
printf '%s  %s\n' "$fsuae_sha" "$staging/fs-uae.tar.xz" | sha256sum --check --status
curl --fail --location --retry 3 --output "$staging/aros.rom.cpp" \
    "https://raw.githubusercontent.com/tonioni/WinUAE/$winuae_revision/aros.rom.cpp"
printf '%s  %s\n' "$aros_source_sha" "$staging/aros.rom.cpp" | sha256sum --check --status
mkdir "$staging/emulator"
tar -xJf "$staging/fs-uae.tar.xz" -C "$staging/emulator"
python3 - "$staging/aros.rom.cpp" "$staging/emulator/aros.rom" <<'PY'
import gzip
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
compressed = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", source))
rom = gzip.decompress(compressed)
if len(rom) != 1024 * 1024:
    raise SystemExit("Unexpected AROS ROM length")
Path(sys.argv[2]).write_bytes(rom)
PY
printf '%s  %s\n' "$aros_rom_sha" "$staging/emulator/aros.rom" | sha256sum --check --status
cat > "$staging/emulator/versions.txt" <<EOF
FS-UAE $fsuae_version: $fsuae_sha
AROS ROM from WinUAE $winuae_revision: $aros_rom_sha
AROS is an open source replacement ROM, not a Commodore Kickstart ROM.
Source and licensing: https://github.com/aros-development-team/AROS
EOF
mkdir -p -- "$(dirname -- "$destination")"
mv -- "$staging/emulator" "$destination"
echo "FS-UAE: $destination/FS-UAE/Linux/x86-64/fs-uae"
echo "AROS ROM: $destination/aros.rom"
echo "Use --kickstart with this ROM in run-amiga-platform-test.py. Xvfb and libXtst are also needed for headless tests."
