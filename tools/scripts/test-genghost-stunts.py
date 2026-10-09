#!/usr/bin/env python3
"""Regression checks for replay stunt coordinates and swept-contact evidence."""

import importlib.util
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
from types import SimpleNamespace
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

    def test_hill_substitutions_match_native_physics_tables(self):
        source = (ROOT / "src/restunts/c/trkutil.c").read_text()
        tables = []
        for name in ("hillroad_pieces", "hillroad_shapes"):
            body = source.split(f"{name}[", 1)[1].split("= {", 1)[1].split("};", 1)[0]
            tables.append(tuple(tuple(map(int, re.findall(r"\d+", row)))
                                for row in re.findall(r"\{([^{}]+)\}", body)))
        self.assertEqual(tuple(map(tuple, STUNTS.HILLROAD_PIECES)), tables[0])
        self.assertEqual(tuple(map(tuple, STUNTS.HILLROAD_SHAPES)), tables[1])
        for terrain, pieces in enumerate(tables[0], 7):
            for index, raw_element in enumerate(pieces):
                with self.subTest(terrain=terrain, raw_element=raw_element):
                    track = bytearray(1802)
                    track[7 * 30 + 5] = raw_element
                    track[901 + 22 * 30 + 5] = terrain
                    native = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024,
                                                native_terrain=True)
                    legacy = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024)
                    expected = tables[1][terrain - 7][min(index, 3)]
                    self.assertEqual((native["raw_element"], native["element"], native["model"]),
                                     (raw_element, expected, self.objects[expected]["model"]))
                    if terrain == 10 and raw_element in (57, 96):
                        self.assertEqual((native["element"], native["model"]), (197, 19))
                    self.assertEqual(legacy["element"], raw_element)
                    self.assertEqual((native["center_x"], native["center_z"]),
                                     (legacy["center_x"], legacy["center_z"]))

    def test_invalid_hill_combination_is_absent_but_flat_stunt_remains(self):
        track = bytearray(1802)
        track[7 * 30 + 5] = 66  # Tunnel: no native hill-road substitution exists.
        track[901 + 22 * 30 + 5] = 7
        self.assertIsNone(STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024,
                                             native_terrain=True))
        legacy = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024)
        self.assertEqual(legacy["model"], 28)
        track[901 + 22 * 30 + 5] = 0
        native = STUNTS.tile_object(track, self.objects, 5.5 * 1024, 7.5 * 1024,
                                    native_terrain=True)
        self.assertEqual((native["element"], native["model"]), (66, 28))

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


class CatalogDiscovery(unittest.TestCase):
    def test_complete_catalog_finds_chicanes_without_changing_legacy_selection(self):
        objects = STUNTS.metadata(ROOT / "src/restunts/c/headless_trackdata.c")
        rows = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive_path = root / "tracks.zip"
            with zipfile.ZipFile(archive_path, "w") as archive:
                for model in (4, 5, 27):
                    track = bytearray(1802)
                    track[0] = next(obj["element"] for obj in objects if obj["model"] == model)
                    name = f"model{model}.rpl"
                    archive.writestr(name, bytes(26) + track)
                    rows.append({"replay": name, "track": f"catalog{model}", "car": "PMIN",
                                 "track_sha256": hashlib.sha256(track).hexdigest(), "verify_exit": 0,
                                 "verified": {"finished": True, "total_frames": 100}})
            args = SimpleNamespace(exclude_manifest=[], validation_tracks=0, telemetry_tracks=2,
                                   output=root, stunt_catalog="legacy")
            with zipfile.ZipFile(archive_path) as archive:
                legacy, _ = STUNTS.select(args, rows, rows, archive, objects)
                args.stunt_catalog = "all"
                complete, _ = STUNTS.select(args, rows, rows, archive, objects)
            self.assertEqual([row["replay"] for row in legacy], ["model27.rpl"])
            self.assertEqual([row["replay"] for row in complete], ["model4.rpl", "model5.rpl"])


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
