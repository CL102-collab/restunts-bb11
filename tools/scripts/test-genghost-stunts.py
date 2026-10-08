#!/usr/bin/env python3
"""Regression checks for replay stunt coordinates and swept-contact evidence."""

import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

SPEC = importlib.util.spec_from_file_location(
    "stunts", Path(__file__).with_name("analyze-genghost-stunts.py"))
STUNTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STUNTS)
ROOT = Path(__file__).resolve().parents[2]


class StuntCoordinates(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.objects = STUNTS.metadata(ROOT / "src/restunts/c/headless_trackdata.c")

    def test_elements_and_terrain_use_opposite_row_order(self):
        track = bytearray(1802)
        track[7 * 30 + 5] = 115
        track[901 + 22 * 30 + 5] = 6
        obj = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024)
        self.assertEqual((obj["element"], obj["model"], obj["row"], obj["raw_row"]),
                         (115, 34, 22, 7))
        self.assertEqual((obj["local_x"], obj["local_z"], obj["base_y"]), (0, 0, 450))

    def test_vertical_continuation_shares_loop_origin(self):
        track = bytearray(1802)
        track[8 * 30 + 5] = 64
        track[7 * 30 + 5] = 254
        first = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024)
        second = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 8.5 * 1024)
        self.assertEqual((first["row"], first["column"]), (second["row"], second["column"]))
        self.assertEqual((first["local_z"], second["local_z"]), (-512, 512))
        self.assertEqual(first["center_z"], 8 * 1024)

    def test_horizontal_continuation_rotates_about_shared_edge(self):
        track = bytearray(1802)
        track[7 * 30 + 5] = 65
        track[7 * 30 + 6] = 255
        left = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024)
        right = STUNTS.tile_object(track, self.objects, 6.5 * 1024, 7.5 * 1024)
        self.assertEqual((left["local_x"], left["local_z"]), (0, -512))
        self.assertEqual((right["local_x"], right["local_z"]), (0, 512))

    def test_rotation_round_trip(self):
        for rotation in (0, 256, 512, 768):
            local = STUNTS.local(123, -456, rotation)
            self.assertEqual(STUNTS.local(*local, (-rotation) % 1024), (123, -456))

    def test_swept_solid_detects_between_frame_tunneling(self):
        stone = ((23, 97), (0, 44), (-271, -241))
        self.assertTrue(STUNTS.segment_box((60, 8, -300), (60, 8, -210), stone))
        self.assertFalse(STUNTS.segment_box((0, 8, -300), (0, 8, -210), stone))
        self.assertFalse(STUNTS.segment_box((60, 60, -300), (60, 60, -210), stone))
        self.assertFalse(STUNTS.segment_box((60, 8, -230), (60, 8, -210), stone))

    def test_off_map_and_malformed_continuations_are_not_elements(self):
        track = bytearray(1802)
        track[29 * 30] = 254
        self.assertIsNone(STUNTS.tile_object(track, self.objects, -1, 0))
        self.assertIsNone(STUNTS.tile_object(track, self.objects, 1, 29.5 * 1024))


class WinnerSelection(unittest.TestCase):
    def test_overall_track_winner_uses_penalty_and_crosses_car_groups(self):
        def row(replay, car, driving, penalty, finished=True, track="same-track"):
            return {"replay": replay, "car": car, "track_sha256": track, "verify_exit": 0,
                    "verified": {"finished": finished, "finish_frame": driving,
                                 "penalty_frames": penalty, "total_frames": driving + penalty}}
        rows = [row("slow.rpl", "JAGU", 200, 0),
                row("penalized.rpl", "PMIN", 50, 200),
                row("winner.rpl", "FGTO", 100, 0),
                row("unfinished.rpl", "COUN", 10, 0, False),
                row("other.rpl", "JAGU", 50, 0, track="different-track")]
        selected = STUNTS.best_track_replays(rows)
        self.assertEqual(selected["same-track"]["replay"], "winner.rpl")
        self.assertEqual(selected["different-track"]["replay"], "other.rpl")
        self.assertEqual(len(selected), 2)

    def test_equal_best_tie_is_deterministic(self):
        rows = [{"replay": name, "car": car, "track_sha256": "same", "verify_exit": 0,
                 "verified": {"finished": True, "total_frames": 100}}
                for name, car in (("z.rpl", "PMIN"), ("a.rpl", "JAGU"))]
        self.assertEqual(STUNTS.best_track_replays(rows)["same"]["replay"], "a.rpl")
        self.assertEqual(STUNTS.best_track_replays(list(reversed(rows)))["same"]["replay"], "a.rpl")


class WheelContactEvidence(unittest.TestCase):
    def test_material_sum_does_not_count_contacting_wheels(self):
        self.assertEqual(STUNTS.wheel_contact_count([2, 0, 0, 0]), 1)
        self.assertEqual(STUNTS.wheel_contact_count([1, 1, 0, 0]), 2)
        self.assertEqual(STUNTS.wheel_contact_count([4, 4, 4, 4]), 4)
        self.assertEqual(STUNTS.wheel_contact_count([0, 0, 0, 0]), 0)

    def test_median_straddle_requires_supported_wheels_in_both_regions(self):
        event = {"model": 11, "origin": [0, 0, 0], "orientation": 0,
                 "samples": [{"frame": 0, "wheels": [[60, 20, 10], [60, 20, -10],
                                                     [200, 0, 10], [200, 0, -10]],
                              "speed_mph": 100, "local": [130, 10, 0], "roll": 0,
                              "contact": 2, "surfaces": [2, 0, 0, 0]}]}
        STUNTS.classify_contacts(event)
        self.assertEqual(event["median_straddle_samples"], [])
        event["samples"][0].update(contact=12, surfaces=[5, 5, 1, 1])
        STUNTS.classify_contacts(event)
        self.assertEqual(len(event["median_straddle_samples"]), 1)


class RankingProvenance(unittest.TestCase):
    def test_prepare_only_does_not_cache_ranking_if_inputs_changed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = root / "analysis"
            archive = root / "replays.zip"
            replay = bytearray(1828)
            replay[:4] = b"LM02"
            struct.pack_into("<HH", replay, 22, 20, 0)
            with zipfile.ZipFile(archive, "w") as target:
                target.writestr("winner.rpl", replay)
            corpus_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
            discovery = root / "discovery.json"
            discovery.write_text(json.dumps({"corpus_sha256": corpus_hash, "recordings": []}))
            metadata_source = root / "metadata.c"
            metadata_source.write_text("metadata")
            track_hash = hashlib.sha256(replay[26:]).hexdigest()
            initial = {"binary_sha256": "original", "corpus_sha256": corpus_hash}
            changed = {**initial, "binary_sha256": "changed-during-verification"}
            arguments = ["analyze-genghost-stunts.py", "--binary", str(root / "engine"),
                         "--repldump", str(root / "driver"), "--data-dir", str(root),
                         "--corpus", str(archive), "--discovery-ranking", str(discovery),
                         "--track-metadata-source", str(metadata_source), "--output", str(output),
                         "--prepare-only"]
            with mock.patch.object(sys, "argv", arguments), \
                    mock.patch.object(STUNTS, "metadata", return_value=[]), \
                    mock.patch.object(STUNTS, "select", return_value=([{"track_sha256": track_hash}], [])), \
                    mock.patch.object(STUNTS.HUMAN, "verify", return_value={"replay": "winner.rpl"}), \
                    mock.patch.object(STUNTS.HUMAN, "run_provenance", side_effect=[initial, changed]):
                with self.assertRaisesRegex(ValueError, "changed during ranking"):
                    STUNTS.main()
            self.assertFalse((output / "ranking.json").exists())
            self.assertFalse((output / "provenance.json").exists())


if __name__ == "__main__":
    unittest.main()
