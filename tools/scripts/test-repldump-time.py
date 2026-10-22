#!/usr/bin/env python3
"""Check optional driving/penalty-time sidecars against native replay states."""

import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile

from regression_timeouts import scaled_timeout


INPUT_OFFSET = 1828
STATE_SIZE = 1120
FINISH_EVENT = 3
FRAME_RATE_OFFSET = 22
NORMAL_FRAME_RATE = 20


def viewer_time(frames, rate):
    """Replay viewer integer quantization, without its leading alignment space."""
    minutes, remainder = divmod(frames, 60 * rate)
    seconds, remainder = divmod(remainder, rate)
    return f"{minutes}:{seconds:02}.{(100 // rate) * remainder:02}".encode("ascii")


def penalty_time(frames, rate):
    minutes, seconds = divmod(frames // rate, 60)
    return f"{minutes}:{seconds:02}".encode("ascii")


def post_finish_fixture(recording):
    # A completed 622-tick lap whose braking tail adds a live 171-second
    # penalty after finishing. Neither that penalty nor the tail is driving time.
    result = bytearray(recording[:INPUT_OFFSET + 642])
    changes = {
        329: 9, 402: 5, 406: 1, 413: 9, 421: 1, 422: 5, 448: 9, 505: 1,
        511: 9, 513: 1, 519: 9, 523: 9, 534: 9, 535: 9, 541: 1,
        542: 1, 547: 5, 548: 5, 549: 1, 550: 1, 557: 9, 558: 9, 559: 5,
        560: 5, 562: 1, 563: 1, 565: 5, 566: 9, 567: 9, 569: 5, 570: 5,
        571: 1, 597: 9, 598: 9, 602: 1, 603: 1, 605: 5, 606: 5, 622: 0,
        623: 0, 624: 0,
    }
    for frame, value in changes.items():
        result[INPUT_OFFSET + frame] = value
    struct.pack_into("<H", result, 24, 642)
    return result


def main():
    replay_driver, game_directory, corpus = (Path(value).resolve() for value in sys.argv[1:])
    available = {p.name.upper() for p in game_directory.iterdir()} if game_directory.is_dir() else set()
    required = (("GAME.RES", "GAME.PRE"), ("CARLM02.RES", "CARLM02.PRE"),
                ("STLM02.P3S", "STLM02.3SH"), ("CARPMIN.RES", "CARPMIN.PRE"),
                ("STPMIN.P3S", "STPMIN.3SH"), ("CARJAGU.RES", "CARJAGU.PRE"),
                ("STJAGU.P3S", "STJAGU.3SH"))
    missing = [" or ".join(names) for names in required if not any(name in available for name in names)]
    if missing or not corpus.is_file():
        print("SKIP: original game resources and golden replays are needed: " + "; ".join(missing))
        return 77
    with zipfile.ZipFile(corpus) as archive:
        ordinary = archive.read("0000.rpl")
        shortcut = archive.read("0722.rpl")
        ten_hz = archive.read("1904.rpl")
    environment = dict(os.environ, SDL_VIDEODRIVER="restunts-invalid-driver",
                       SDL_AUDIODRIVER="restunts-invalid-driver", RESTUNTS_AUDIO_TRACE="")
    with tempfile.TemporaryDirectory(prefix="rtim-") as temporary:
        directory = Path(temporary)

        def run(stem, flags=(), success=True):
            source = directory / (stem + ".RPL")
            result = subprocess.run([str(replay_driver), "--data-dir", str(game_directory),
                                     str(source), "1", *flags], cwd=directory, env=environment,
                                    capture_output=True, text=True, timeout=scaled_timeout(30))
            assert (result.returncode == 0) == success, (result.returncode, result.stdout, result.stderr)
            return result

        def states(stem):
            recording = (directory / (stem + ".RPL")).read_bytes()
            dump = (directory / (stem + ".BNI")).read_bytes()
            count = struct.unpack_from("<H", dump)[0]
            assert count == struct.unpack_from("<H", recording, 24)[0]
            assert len(dump) == 2 + count * STATE_SIZE
            return dump, dump[-STATE_SIZE:]

        def verify_time(stem, recording, flags=(), expected=None):
            (directory / (stem + ".RPL")).write_bytes(recording)
            run(stem, ("--time", *flags))
            dump, final = states(stem)
            value = (directory / (stem + ".tim")).read_bytes()
            rate = struct.unpack_from("<H", recording, FRAME_RATE_OFFSET)[0]
            finish = struct.unpack_from("<H", final, 0x146)[0]
            total = struct.unpack_from("<H", final, 0x142)[0]
            wanted = (viewer_time(finish, rate) + b"\r\n" + penalty_time((total - finish) & 0xFFFF, rate)
                      if final[0x21B] == FINISH_EVENT else b"")
            assert value == wanted, (stem, value, wanted)
            if expected is not None:
                assert value == expected, (stem, value, expected)
            assert b"\0" not in value and not value.endswith((b"\r", b"\n"))
            assert value.count(b"\r\n") == (1 if value else 0)
            without_separator = value.replace(b"\r\n", b"")
            assert b"\r" not in without_separator and b"\n" not in without_separator
            return final, dump

        # With no option, a sidecar is neither created nor overwritten. Asking
        # for time must also preserve the exact normal serialized replay output.
        source = directory / "plain.RPL"
        source.write_bytes(ordinary)
        run("plain")
        assert not source.with_suffix(".tim").exists()
        original_dump = source.with_suffix(".BNI").read_bytes()
        source.with_suffix(".tim").write_bytes(b"keep existing time")
        run("plain")
        assert source.with_suffix(".tim").read_bytes() == b"keep existing time"
        run("plain", ("--TiMe", "--time"))
        assert source.with_suffix(".tim").read_bytes() == b"1:53.60\r\n0:00"
        assert source.with_suffix(".BNI").read_bytes() == original_dump

        # The real switched replay has 198 seconds of penalty in fixed mode.
        # Its driving time remains 31.25 seconds, independently of its score.
        final, _ = verify_time("penalty", shortcut, ("--dws:off",), b"0:31.25\r\n3:18")
        total, finish = (struct.unpack_from("<H", final, offset)[0] for offset in (0x142, 0x146))
        assert finish == 625 and total - finish == 3960
        assert viewer_time(total, 20) != b"0:31.25"

        # The stored rate controls simulation even without a time report.
        # Changing only that rate must change the dump; asking for a report
        # must not change simulation or its serialized output.
        normal_rate = bytearray(ten_hz)
        struct.pack_into("<H", normal_rate, FRAME_RATE_OFFSET, NORMAL_FRAME_RATE)
        (directory / "ten20.RPL").write_bytes(normal_rate)
        run("ten20")
        normal_dump, _ = states("ten20")
        (directory / "tenbase.RPL").write_bytes(ten_hz)
        run("tenbase")
        recorded_dump, _ = states("tenbase")
        assert recorded_dump != normal_dump, "Replay simulation must honor the stored frame rate"

        final, timed_dump = verify_time("tenhz", ten_hz, expected=b"1:38.70\r\n0:00")
        assert struct.unpack_from("<H", ten_hz, FRAME_RATE_OFFSET)[0] == 10
        assert struct.unpack_from("<H", final, 0x146)[0] == 987
        assert timed_dump == recorded_dump, "Time reporting must not change replay simulation"

        final, _ = verify_time("tail", post_finish_fixture(shortcut), expected=b"0:31.10\r\n0:00")
        assert struct.unpack_from("<H", final, 0x14A)[0] == 3420
        assert struct.unpack_from("<H", final, 0x140)[0] == 642

        # A valid partial replay and an OWOOT crash cannot retain a completed
        # time from an earlier invocation. OWOOT still writes its own verdict.
        unfinished = bytearray(ordinary[:INPUT_OFFSET + 10])
        struct.pack_into("<H", unfinished, 24, 10)
        (directory / "partial.tim").write_bytes(b"3:03.00\r\n0:30")
        final, _ = verify_time("partial", unfinished, expected=b"")
        assert final[0x21B] == 0
        (directory / "crash.tim").write_bytes(b"3:03.00\r\n0:30")
        final, _ = verify_time("crash", shortcut, ("--owoot",), b"")
        assert final[0x21B] not in (0, FINISH_EVENT)
        assert (directory / "crash.owo").read_bytes() == b"fail"

        # Stale success is cleared before a replay/resource error can abort the
        # driver. A sidecar create failure is reported instead of silent success.
        (directory / "missing.tim").write_bytes(b"3:03.00\r\n0:30")
        run("missing", ("--time",), success=False)
        assert (directory / "missing.tim").read_bytes() == b""
        (directory / "blocked.RPL").write_bytes(ordinary)
        (directory / "blocked.tim").mkdir()
        (directory / "blocked.tim" / "keep").write_bytes(b"keep")
        run("blocked", ("--time",), success=False)
        assert (directory / "blocked.tim" / "keep").read_bytes() == b"keep"
    print("Replay dumps honor recorded rates with or without time reports; .tim driving and "
          "latched penalty times match at 20/10 Hz; tails, "
          "incomplete/crashed runs, optional output, stale results, I/O errors, and OWOOT passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
