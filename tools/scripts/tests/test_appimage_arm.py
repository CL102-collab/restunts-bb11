"""Check ARM userspace isolation and bounded PCM collection without target binaries."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "test-appimage-arm.py"
SPEC = importlib.util.spec_from_file_location("test_appimage_arm_driver", SCRIPT)
DRIVER = importlib.util.module_from_spec(SPEC)
with mock.patch.object(sys, "path", [str(SCRIPT.parent)] + sys.path):
    SPEC.loader.exec_module(DRIVER)


class ArmAppImageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="arm-compat-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.sysroot, self.libraries = (self.root / name for name in ("sysroot", "AppDir/usr/lib"))
        self.sysroot.mkdir()
        self.libraries.mkdir(parents=True)

    def test_target_library_path_never_reaches_the_host_process(self):
        with mock.patch.dict(os.environ, {"LD_LIBRARY_PATH": "/wrong/host-libs",
                                         "QEMU_LD_PREFIX": "/wrong/libc",
                                         "QEMU_SET_ENV": "LD_PRELOAD=wrong.so",
                                         "APPDIR": "/wrong/appdir",
                                         "APPIMAGE_EXTRACT_AND_RUN": "1"}):
            environment = DRIVER.host_environment()
        environment = DRIVER.target_environment(environment, {"LD_LIBRARY_PATH": str(self.libraries)})
        output = subprocess.check_output([
            sys.executable, "-c", "import json,os; print(json.dumps(dict(os.environ)))"],
            env=environment, text=True)
        actual = json.loads(output)
        self.assertNotIn("LD_LIBRARY_PATH", actual)
        self.assertNotIn("QEMU_LD_PREFIX", actual)
        self.assertNotIn("APPDIR", actual)
        self.assertNotIn("APPIMAGE_EXTRACT_AND_RUN", actual)
        self.assertEqual(actual["QEMU_SET_ENV"], "LD_LIBRARY_PATH=" + str(self.libraries))

    def test_loader_paths_resolve_only_inside_sdk_or_appimage(self):
        libc = self.sysroot / "lib/libc.so.6"
        libc.parent.mkdir()
        libc.write_bytes(b"historical libc")
        bundled = self.libraries / "libaudio.so.1"
        bundled.write_bytes(b"bundled dependency")
        self.assertEqual(DRIVER.resolve_dependency("/lib/libc.so.6", self.sysroot, self.libraries), libc)
        self.assertEqual(DRIVER.resolve_dependency(str(bundled), self.sysroot, self.libraries), bundled)
        host = self.root / "host.so"
        host.write_bytes(b"unwanted host dependency")
        with self.assertRaisesRegex(RuntimeError, "escaped pinned SDK"):
            DRIVER.resolve_dependency(str(host), self.sysroot, self.libraries)
        libc.unlink()
        libc.symlink_to(host)
        with self.assertRaisesRegex(RuntimeError, "escaped pinned SDK"):
            DRIVER.resolve_dependency("/lib/libc.so.6", self.sysroot, self.libraries)

    @unittest.skipUnless(hasattr(os, "mkfifo"), "PCM collection uses a POSIX FIFO")
    def test_pcm_capture_discards_initial_silence_and_keeps_a_bounded_sample(self):
        producer = self.root / "producer.py"
        producer.write_text("""import os,re,time
from pathlib import Path
configuration = Path(os.environ["ALSA_CONFIG_PATH"]).read_text()
fifo = re.search(r'file "([^"]+)"', configuration).group(1)
with open(fifo, "wb", buffering=0) as output:
    output.write(bytes(int(os.environ["PCM_TEST_BYTES"])))
    while True:
        output.write(b"\\x7f" * int(os.environ["PCM_TEST_BYTES"]))
        time.sleep(0.01)
""")
        def environment(extra):
            return dict(DRIVER.host_environment(), **extra, PCM_TEST_BYTES=str(DRIVER.PCM_BYTES))
        DRIVER.capture_audio([sys.executable, str(producer)], environment, self.root, self.root)
        self.assertEqual((self.root / "audio.raw").read_bytes(), b"\x7f" * DRIVER.PCM_BYTES)


if __name__ == "__main__":
    unittest.main()
