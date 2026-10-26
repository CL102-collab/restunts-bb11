#!/usr/bin/env python3
"""Exercise the portable graceful-stop protocol used by the desktop GUI."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zipfile

from regression_timeouts import scaled_timeout


POLL_SECONDS = 0.02
EMPTY_FILE_SECONDS = 0.3
START_TIMEOUT_SECONDS = 15
STOP_TIMEOUT_SECONDS = 15
SEED_REPLAY = "0722.rpl"


def main():
    if len(sys.argv) != 4:
        print("Usage: test-genghost-stop.py GENGHOST DATA_DIR CORPUS")
        return 1
    binary, data, corpus = (Path(value).resolve() for value in sys.argv[1:])
    available = {path.name.upper() for path in data.iterdir()} if data.is_dir() else set()
    required = (("GAME.RES", "GAME.PRE"), ("CARPMIN.RES", "CARPMIN.PRE"),
                ("STPMIN.P3S", "STPMIN.3SH"))
    missing = [" or ".join(names) for names in required if not available.intersection(names)]
    if missing or not corpus.is_file():
        print("SKIP: Stunts resources and golden replays are needed: " + "; ".join(missing))
        return 77
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with zipfile.ZipFile(corpus) as archive:
        seed = archive.read(SEED_REPLAY)
    car = seed[:4].decode("ascii")
    with tempfile.TemporaryDirectory(prefix="gh stop-") as directory:
        temporary = Path(directory)
        source = temporary / "source.RPL"
        source.write_bytes(seed)

        def command(arguments):
            return [str(binary), "--data-dir", str(data), *map(str, arguments)]

        def score(path):
            result = subprocess.run(command([path, car, "--verify"]), cwd=temporary,
                                    env=environment, capture_output=True, text=True,
                                    timeout=scaled_timeout(STOP_TIMEOUT_SECONDS))
            assert result.returncode == 0, (result.stdout, result.stderr)
            return json.loads(result.stdout)

        baseline = score(source)
        for mode in ("generate", "--fine-tune", "--fine-tune-gears-only"):
            for strategy in ("legacy", "state"):
                for cores in (1, 2):
                    name = f"{mode.lstrip('-')}-{strategy}-{cores}"
                    output = temporary / (name + ".rpl")
                    report_path = output.with_name(output.name + ".json")
                    log_path = temporary / (name + ".log")
                    control = temporary / "stop control"
                    control.write_bytes(b"")
                    arguments = [source, "--strategy", strategy, "--cores", cores,
                                 "--seconds", 0, "--output", output,
                                 "--stop-file", control.name]
                    arguments.append(car if mode == "generate" else mode)
                    with log_path.open("w") as log:
                        process = subprocess.Popen(command(arguments), cwd=temporary,
                                                   env=environment, stdout=log, stderr=log)
                        try:
                            started = time.monotonic()
                            deadline = started + scaled_timeout(START_TIMEOUT_SECONDS)
                            while time.monotonic() < deadline:
                                assert process.poll() is None, log_path.read_text()
                                if (report_path.is_file() and
                                        time.monotonic() - started >= EMPTY_FILE_SECONDS):
                                    break
                                time.sleep(POLL_SECONDS)
                            else:
                                raise AssertionError("No saved incumbent while stop file was empty:\n"
                                                     + log_path.read_text())
                            control.write_text("stop\n")
                            assert process.wait(timeout=scaled_timeout(STOP_TIMEOUT_SECONDS)) == 0, (
                                log_path.read_text())
                        finally:
                            if process.poll() is None:
                                process.kill()
                                process.wait(timeout=scaled_timeout(STOP_TIMEOUT_SECONDS))
                    assert control.read_text() == "stop\n", "Generator must not remove caller's stop file"
                    report = json.loads(report_path.read_text())
                    verified = score(output)
                    assert verified["finished"] and report["finished"], (report, verified)
                    assert report["total_frames"] == verified["total_frames"]
                    assert report["total_frames"] <= baseline["total_frames"], (baseline, report)
                    assert report["strategy"] == strategy
                    assert source.read_bytes() == seed, "Graceful stop modified the seed replay"
                    assert not list(temporary.glob("*.work-*")), "Private worker files survived stop"
                    control.unlink()
        print("Empty stop files stay active; nonempty files gracefully stop all desktop search modes, "
              "preserve verified incumbents, and clean up workers.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
