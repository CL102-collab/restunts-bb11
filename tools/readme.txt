The tools directory contains make and setup
scripts for the active Open Watcom 2 compiler, assembler, and linker.

Install the pinned toolchain from the repository root:
  Windows: powershell -ExecutionPolicy Bypass -File tools\scripts\install-open-watcom.ps1
  Linux:   tools/scripts/install-open-watcom.sh

Both installers verify the SHA256 in scripts/open-watcom.conf and extract into
ignored tools/watcom. A different or incomplete installation must be moved
aside before reinstalling.

setpath.bat puts watcom\binnt before bin on PATH, sets WATCOM to the pinned
installation, and selects its C headers. WCC, WASM, and WLINK come from that
installation; GNU Make 4.4.1 remains bundled for Windows. GNU Make 4.3 or newer
is required for grouped generated-source targets. Linux uses the native
binl64 tools with system GNU Make, without Wine or DOSBox for compilation.
Python 3.9 or newer is needed for the WASM original-game source adapter. The source ASM
files remain unchanged; generated compatible copies live in the build tree.

C builds use only Open Watcom headers and runtime libraries under watcom.
The immutable pre-migration regression binaries live in oracles/borland.

See docs/devs.md for build commands and regression validation, and
docs/assembler.md for original-source preparation and artifact comparisons.
