# LLVM runtime notices

Windows ARM64 uses the pinned LLVM-MinGW 20260922 release, which contains
LLVM 23.1.2 and its C++/unwinding/compiler support runtimes. The exact upstream
license files are preserved here and installed under `share/licenses/llvm-runtime`.
They include Apache License 2.0 with LLVM Exceptions and the earlier license
terms retained by each component. These terms do not change the licenses of
unrelated project code or assets.

Toolchain release: https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922

The release archive is selected and SHA-256 verified by
`tools/scripts/package-toolchains.conf`. These license files come from the
official `llvmorg-23.1.2` source tag:

- `libcxx-LICENSE.txt`: https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-23.1.2/libcxx/LICENSE.TXT
  SHA-256 with canonical LF line endings: `539dd7aed86e8a4f12cbdd0e6c50c189c7d74847e4fecc64ce2c6ee3a01da38b`.
- `libcxxabi-LICENSE.txt`: https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-23.1.2/libcxxabi/LICENSE.TXT
  SHA-256 with canonical LF line endings: `e2b35be49f7284a45b7baca8fc7b3ab7440e7902392b2528a457816b5bb2a15c`.
- `libunwind-LICENSE.txt`: https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-23.1.2/libunwind/LICENSE.TXT
  SHA-256 with canonical LF line endings: `b5efebcaca80879234098e52d1725e6d9eb8fb96a19fce625d39184b705f7b6d`.
- `compiler-rt-LICENSE.txt`: https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-23.1.2/compiler-rt/LICENSE.TXT
  SHA-256 with canonical LF line endings: `1a8f1058753f1ba890de984e48f0242a3a5c29a6a8f2ed9fd813f36985387e8d`.
