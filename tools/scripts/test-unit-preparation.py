#!/usr/bin/env python3
"""Reject incomplete fixed UNIT preparation instead of launching a partial race."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from regression_timeouts import scaled_timeout


REQUESTED_STEPS = 1024
ACCEPTED_STEPS = 32
IMPOSSIBLE_DEADLINE_MS = 1
PREPARATION_DEADLINE_MS = 10000
PROCESS_TIMEOUT_SECONDS = 30


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("data", type=Path)
    parser.add_argument("track", type=Path)
    args = parser.parse_args()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    with tempfile.TemporaryDirectory(prefix="restunts-unit-preparation-") as temporary:
        def race(label, preparation):
            cache = Path(temporary) / label
            cache.mkdir()
            command = [str(args.native.resolve()), "--data-dir", str(args.data.resolve()),
                       str(args.track.resolve()), "7", "--car", "PMIN", "--manual",
                       "--planner-sync", "--frames", "1", "--verify-replay",
                       "--unit-cache-dir", str(cache), *preparation]
            return subprocess.run(command, env=env, text=True, capture_output=True,
                                  timeout=scaled_timeout(PROCESS_TIMEOUT_SECONDS))

        def require_rejection(label, preparation):
            result = race(label, preparation)
            output = result.stdout + result.stderr
            assert result.returncode != 0, (label, output)
            assert "preparation" in output.lower(), (label, output)
            assert "opponent=" not in output, (label, output)
            assert "replay_verified" not in output, (label, output)

        require_rejection("missing-watchdog", ["--prepare-steps", str(ACCEPTED_STEPS)])
        require_rejection("zero-watchdog", ["--prepare-steps", str(ACCEPTED_STEPS),
                                             "--prepare-ms", "0"])
        require_rejection("exhausted-watchdog", ["--prepare-steps", str(REQUESTED_STEPS),
                                                  "--prepare-ms", str(IMPOSSIBLE_DEADLINE_MS)])
        result = race("complete-work", ["--prepare-steps", str(ACCEPTED_STEPS),
                                        "--prepare-ms", str(scaled_timeout(
                                            PREPARATION_DEADLINE_MS))])
        assert result.returncode == 0, (result.stdout, result.stderr)
        fields = {key: float(value) for key, value in
                  re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", result.stdout)}
        assert fields["fixed_work"] == 1, result.stdout
        assert fields["preparation_steps"] == ACCEPTED_STEPS, result.stdout
        assert fields["frames"] == fields["replay_verified_frames"] == 1, result.stdout
        print("UNIT fixed preparation rejects incomplete work and runs exactly 32 preparation callbacks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
