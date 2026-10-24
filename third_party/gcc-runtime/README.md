# GCC runtime notices

Windows x86/x64 launcher builds use GCC's static libstdc++ and libgcc runtimes.
The runtime libraries use GNU GPL version 3 or later with GCC Runtime Library
Exception version 3.1. The full terms are preserved in `COPYING3.txt` and
`COPYING.RUNTIME.txt`. These files describe the compiler runtime; they do not
change the licenses of unrelated project code or assets.

The license texts were copied from the official GCC 12.2.0 release tag:

- https://github.com/gcc-mirror/gcc/blob/releases/gcc-12.2.0/COPYING3
- https://github.com/gcc-mirror/gcc/blob/releases/gcc-12.2.0/COPYING.RUNTIME

`gcc-mingw-w64-copyright.txt` is the unchanged Debian copyright metadata from
the verified `gcc-mingw-w64-base` package used for the Windows XP build:

- Version: `12.2.0-14+deb12u1+25.2+b1` (`amd64`).
- Source: https://deb.debian.org/debian/pool/main/g/gcc-mingw-w64/gcc-mingw-w64-base_12.2.0-14+deb12u1+25.2+b1_amd64.deb
- Package SHA-256: `811339affa3a6cf974bcfe2c2be3ce2d8883a43f45934ea7349f1c26b932ee38`.
- Package member: `usr/share/doc/gcc-mingw-w64-base/copyright`.

Debian's metadata still refers to an older GCC source package in its prose;
it is retained verbatim. It includes notices for additional compiler components
that this launcher does not use.

Upstream/member SHA-256 values, with canonical LF line endings:

| File | SHA-256 |
| --- | --- |
| `COPYING3.txt` | `8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903` |
| `COPYING.RUNTIME.txt` | `9d6b43ce4d8de0c878bf16b54d8e7a10d9bd42b75178153e3af6a815bdc90f74` |
| `gcc-mingw-w64-copyright.txt` | `a481f772f7a53335f13b32c6c54eb1c8577ce97704edd3757ab7ed4287a8e96a` |

CMake installs these notices in `share/licenses/gcc-runtime` for GNU Windows
launcher builds. AppImages preserve the matching license texts from their
separate pinned GCC 4.8.2 SDK. Windows ARM64 uses LLVM's runtime instead.
