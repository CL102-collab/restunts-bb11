"""Check the missing compiler builtins used by NetBSD's wide-character headers."""

from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "cmake/toolchains/netbsd-clang-compat.h"


class NetBsdClangCompatibilityTests(unittest.TestCase):
    def setUp(self):
        self.compiler = shutil.which("cc")
        if not self.compiler:
            self.skipTest("A C compiler is needed for the NetBSD header probe")

    def preprocess(self, source):
        result = subprocess.run([self.compiler, "-E", "-x", "c", "-"], input=source,
                                text=True, capture_output=True, timeout=15, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_minima_follow_each_types_signedness(self):
        for wchar_unsigned in (False, True):
            for wint_unsigned in (False, True):
                with self.subTest(wchar_unsigned=wchar_unsigned, wint_unsigned=wint_unsigned):
                    source = []
                    for name, unsigned in (("WCHAR", wchar_unsigned), ("WINT", wint_unsigned)):
                        source.extend([f"#undef __{name}_MIN__", f"#undef __{name}_MAX__",
                                       f"#undef __{name}_UNSIGNED__"])
                        if unsigned:
                            source.extend([f"#define __{name}_UNSIGNED__ 1",
                                           f"#define __{name}_MAX__ (__INT_MAX__ * 2U + 1U)"])
                        else:
                            source.append(f"#define __{name}_MAX__ __INT_MAX__")
                    source.append(f'#include "{HEADER}"')
                    for name, unsigned in (("WCHAR", wchar_unsigned), ("WINT", wint_unsigned)):
                        expected = "0" if unsigned else "(-__INT_MAX__ - 1)"
                        source.extend([f"#if __{name}_MIN__ != {expected}",
                                       f'#error "Wrong {name} minimum"', "#endif"])
                    self.preprocess("\n".join(source) + "\n")

    def test_missing_signal_type_matches_the_compiler_width_and_signedness(self):
        source = f'''
#undef __SIG_ATOMIC_TYPE__
#include "{HEADER}"
_Static_assert(sizeof(__SIG_ATOMIC_TYPE__) * __CHAR_BIT__ == __SIG_ATOMIC_WIDTH__,
               "Signal type width does not match the target");
_Static_assert((__SIG_ATOMIC_TYPE__)(__SIG_ATOMIC_MAX__) == __SIG_ATOMIC_MAX__,
               "Signal type cannot represent the target maximum");
_Static_assert((__SIG_ATOMIC_TYPE__)(-1) < 0, "Expected a signed signal type");
'''
        result = subprocess.run([self.compiler, "-fsyntax-only", "-x", "c", "-"], input=source,
                                text=True, capture_output=True, timeout=15, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_existing_compiler_minima_are_preserved(self):
        self.preprocess(f'''
#undef __WCHAR_MIN__
#undef __WINT_MIN__
#define __WCHAR_MIN__ (-__WCHAR_MAX__)
#define __WINT_MIN__ (-__WINT_MAX__)
#include "{HEADER}"
#if __WCHAR_MIN__ != -__WCHAR_MAX__ || __WINT_MIN__ != -__WINT_MAX__
#error "Existing compiler minima changed"
#endif
''')


if __name__ == "__main__":
    unittest.main()
