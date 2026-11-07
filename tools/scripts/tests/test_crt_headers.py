"""Compile host CRT headers around the legacy string aliases in both orders."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[3]
COMPILE_TIMEOUT_SECONDS = 60
CROSS_COMPILERS = ("i686-w64-mingw32-gcc", "x86_64-w64-mingw32-gcc",
                   "i686-w64-mingw32-clang", "x86_64-w64-mingw32-clang",
                   "aarch64-w64-mingw32-clang")
HOST_HEADERS = "#include <string.h>\n#include <stdlib.h>\n#include <wchar.h>\n"
LEGACY_HEADER = '#include "externs.h"\n'
COPY_SIGNATURES = """void *(*legacy_copy)(void *, const void *, legacy_u16) = memcpy;
#undef memcpy
void *(*host_copy)(void *, const void *, size_t) = memcpy;
"""


class CrtHeaderTests(unittest.TestCase):
    def test_host_headers_preserve_native_and_legacy_copy_signatures(self):
        compilers = [shlex.split(os.environ.get("CC", "cc"))]
        compilers.extend([compiler] for compiler in CROSS_COMPILERS if shutil.which(compiler))
        if not shutil.which(compilers[0][0]):
            self.skipTest("A host C compiler is needed for the header probe")
        for compiler in compilers:
            for order in ("host-first", "legacy-first"):
                with self.subTest(compiler=compiler, order=order):
                    headers = (HOST_HEADERS + LEGACY_HEADER if order == "host-first"
                               else LEGACY_HEADER + HOST_HEADERS)
                    result = subprocess.run(
                        compiler + ["-std=c99", "-Werror", "-DRESTUNTS_SDL3", "-I",
                                    str(ROOT / "src/restunts/c"), "-x", "c", "-fsyntax-only", "-"],
                        input=headers + COPY_SIGNATURES, text=True, capture_output=True,
                        timeout=COMPILE_TIMEOUT_SECONDS, check=False)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
