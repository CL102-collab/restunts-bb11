"""Exercise worker probes while Linux procfs entries disappear during reads."""

import errno
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock


SCRIPTS = Path(__file__).resolve().parents[1]
PARENT_PID = 101
WORKER_PID = 102
CHILD_SLEEP_SECONDS = 30
CHILD_WAIT_TIMEOUT = 5


def load_script(name):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    with mock.patch.object(sys, "path", [str(SCRIPTS)] + sys.path):
        spec.loader.exec_module(module)
    return module


PARALLEL = load_script("test-genghost-parallel")
STATE = load_script("test-genghost-state")
LIVENESS_PROBES = (PARALLEL.process_live, STATE.alive)
CHILD_PROBES = ((PARALLEL.process_workers, {}), (STATE.children, []))


class GenghostProcessProbeTests(unittest.TestCase):
    def test_disappeared_process_is_not_live(self):
        for probe in LIVENESS_PROBES:
            for error in (FileNotFoundError(errno.ENOENT, "No such file"),
                          ProcessLookupError(errno.ESRCH, "No such process")):
                with self.subTest(probe=probe.__name__, error=type(error).__name__), \
                        mock.patch.object(Path, "read_text", side_effect=error):
                    self.assertFalse(probe(WORKER_PID))

    def test_live_and_zombie_states_remain_distinct(self):
        for probe in LIVENESS_PROBES:
            for state, expected in (("R", True), ("S", True), ("D", True),
                                    ("T", True), ("Z", False)):
                record = f"{WORKER_PID} (ghost worker (test)) {state} {PARENT_PID} 0 0\n"
                with self.subTest(probe=probe.__name__, state=state), \
                        mock.patch.object(Path, "read_text", return_value=record):
                    self.assertEqual(probe(WORKER_PID), expected)

    def test_child_list_disappearance_falls_back_to_process_scan(self):
        for probe, empty in CHILD_PROBES:
            for error in (FileNotFoundError(errno.ENOENT, "No such file"),
                          ProcessLookupError(errno.ESRCH, "No such process")):
                with self.subTest(probe=probe.__name__, error=type(error).__name__), \
                        mock.patch.object(Path, "read_text", side_effect=error), \
                        mock.patch.object(Path, "iterdir", return_value=iter(())):
                    self.assertEqual(probe(PARENT_PID), empty)

    def test_unexpected_io_errors_are_not_treated_as_process_exit(self):
        for probe in (*LIVENESS_PROBES, *(probe for probe, _ in CHILD_PROBES)):
            for error in (PermissionError(errno.EACCES, "Permission denied"),
                          OSError(errno.EIO, "I/O error")):
                with self.subTest(probe=probe.__name__, error=error.errno), \
                        mock.patch.object(Path, "read_text", side_effect=error):
                    with self.assertRaises(type(error)):
                        probe(WORKER_PID)

    @unittest.skipUnless(sys.platform == "linux" and Path("/proc/self/stat").is_file(),
                         "The exit-during-read reproduction requires Linux procfs")
    def test_process_reaped_after_stat_open_is_not_live(self):
        original_open = Path.open
        for probe in LIVENESS_PROBES:
            with self.subTest(probe=probe.__name__):
                child = subprocess.Popen([
                    sys.executable, "-c", f"import time; time.sleep({CHILD_SLEEP_SECONDS})"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                try:
                    def open_then_reap(path, *args, **kwargs):
                        self.assertEqual(path, Path(f"/proc/{child.pid}/stat"))
                        stream = original_open(path, *args, **kwargs)
                        child.kill()
                        child.wait(timeout=CHILD_WAIT_TIMEOUT)
                        return stream

                    # Keep the procfs file descriptor open across process exit:
                    # Linux reports ESRCH from read(), rather than ENOENT at open().
                    with mock.patch.object(Path, "open", autospec=True,
                                           side_effect=open_then_reap):
                        self.assertFalse(probe(child.pid))
                finally:
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=CHILD_WAIT_TIMEOUT)


if __name__ == "__main__":
    unittest.main()
