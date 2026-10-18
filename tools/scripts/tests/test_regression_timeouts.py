"""Check instrumented subprocess limits without weakening normal-build limits."""

import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "regression_timeouts.py"
SPEC = importlib.util.spec_from_file_location("regression_timeouts", SCRIPT)
TIMEOUTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TIMEOUTS)
VARIABLE = TIMEOUTS.TIMEOUT_MULTIPLIER_VARIABLE


class RegressionTimeoutTests(unittest.TestCase):
    def setUp(self):
        self.environment = mock.patch.dict(os.environ)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        os.environ.pop(VARIABLE, None)

    def test_default_preserves_existing_limits(self):
        self.assertEqual(TIMEOUTS.scaled_timeout(90), 90)
        self.assertEqual(TIMEOUTS.scaled_timeout(0.5), 0.5)

    def test_override_scales_each_limit_once(self):
        os.environ[VARIABLE] = "10"
        self.assertEqual(TIMEOUTS.scaled_timeout(90), 900)
        self.assertEqual(TIMEOUTS.scaled_timeout(40), 400)
        self.assertEqual(TIMEOUTS.scaled_timeout(0.5), 5)

    def test_invalid_override_is_rejected(self):
        for value in ("", "0", "-1", "1.5", "abc", " 10", "10 ", "01", "1\n", "１０"):
            with self.subTest(value=value):
                os.environ[VARIABLE] = value
                with self.assertRaisesRegex(ValueError, VARIABLE + " must be a positive integer"):
                    TIMEOUTS.scaled_timeout(90)

    def test_sleeping_child_needs_the_override(self):
        # The same child exceeds the normal limit but fits comfortably inside
        # the instrumented limit. Assert subprocess outcomes, not elapsed time.
        command = [sys.executable, "-c", "import time; time.sleep(1.5)"]
        with self.assertRaises(subprocess.TimeoutExpired):
            subprocess.run(command, check=True, timeout=TIMEOUTS.scaled_timeout(0.5))
        os.environ[VARIABLE] = "10"
        subprocess.run(command, check=True, timeout=TIMEOUTS.scaled_timeout(0.5))


if __name__ == "__main__":
    unittest.main()
