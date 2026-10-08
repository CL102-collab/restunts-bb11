"""Guard the different row orders of replay objects and native route coordinates."""

import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "analyze-genghost-replays.py"
SPEC = importlib.util.spec_from_file_location("analyze_genghost_replays", SCRIPT)
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


class ReplayTileTests(unittest.TestCase):
    def setUp(self):
        # Replay objects run south to north, opposite to route rows. Distinct
        # corners catch a vertical flip, column transpose, or header offset.
        south = bytes([115] + [0] * 28 + [116])
        north = bytes([64] + [0] * 28 + [65])
        self.replay = bytes([255] * 26) + south + bytes(28 * 30) + north

    def test_asymmetric_object_rows(self):
        self.assertEqual(ANALYSIS.replay_tile_id(self.replay, 0, 0), 64)
        self.assertEqual(ANALYSIS.replay_tile_id(self.replay, 29, 0), 65)
        self.assertEqual(ANALYSIS.replay_tile_id(self.replay, 0, 29), 115)
        self.assertEqual(ANALYSIS.replay_tile_id(self.replay, 29, 29), 116)
        self.assertEqual(ANALYSIS.replay_tile_id(self.replay, 12, 7), 0)

    def test_outside_map_is_not_a_wrapped_lookup(self):
        for column, row in ((-1, 0), (30, 0), (0, -1), (0, 30)):
            with self.subTest(column=column, row=row):
                self.assertIsNone(ANALYSIS.replay_tile_id(self.replay, column, row))


class ReplaySelectionTests(unittest.TestCase):
    def test_only_overall_winner_per_exact_track(self):
        def recording(name, car, score, finished=True, track_hash="track-a"):
            return {"replay": name, "car": car, "track": "SAME-NAME",
                    "track_sha256": track_hash, "verify_exit": 0,
                    "verified": {"finished": finished, "total_frames": score}}

        rows = [recording("slow.rpl", "COUN", 200),
                recording("winner.rpl", "PMIN", 100),
                recording("tie.rpl", "PMIN", 100),
                recording("crashed.rpl", "COUN", 20, finished=False),
                recording("other.rpl", "COUN", 300, track_hash="track-b")]
        selected = ANALYSIS.select_cases(rows, {"track-a", "track-b"}, 2)
        self.assertEqual({row["track_sha256"]: row["replay"] for row in selected},
                         {"track-a": "tie.rpl", "track-b": "other.rpl"})
        self.assertEqual(selected, ANALYSIS.select_cases(list(reversed(rows)),
                                                        {"track-a", "track-b"}, 2))


if __name__ == "__main__":
    unittest.main()
