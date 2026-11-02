#!/usr/bin/env python3
"""Check bundled cars and private resource-folder transfers without a guest."""

from pathlib import Path
import runpy
import struct
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parent
PREPARE = runpy.run_path(str(TOOLS / "PREPARE.PY"))
DATA_CD = runpy.run_path(str(TOOLS / "MAKE_DATA_CD.PY"))
BUNDLED_NAMES = {
    "CARMCMU.RES", "CARTERR.RES", "STDAMCMU.VSH", "STDATERR.VSH",
    "STDBMCMU.VSH", "STDBTERR.VSH", "STMCMU.3SH", "STTERR.3SH",
}


def directory_entries(image, cluster, size):
    entries = {}
    sector_bytes = DATA_CD["SECTOR_BYTES"]
    record_bytes = DATA_CD["DIRECTORY_ENTRY_BYTES"]
    for offset in range(cluster * sector_bytes, cluster * sector_bytes + size, record_bytes):
        attributes, name, child_cluster, child_size, _ = struct.unpack_from("<H38sqqQ", image, offset)
        if not attributes:
            break
        entries[name.rstrip(b"\x00").decode("ascii")] = (attributes, child_cluster, child_size)
    return entries


class TemplePackageTests(unittest.TestCase):
    def test_public_package_places_only_bundled_resources_beside_code(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "public"
            PREPARE["prepare"](None, destination)
            self.assertEqual(list((destination / "DATA").iterdir()), [])
            self.assertTrue((destination / "PORT/LOAD.HC").is_file())
            cars = destination / "PORT/CARS"
            self.assertEqual({path.name for path in cars.iterdir()}, BUNDLED_NAMES)
            for name in BUNDLED_NAMES:
                self.assertEqual((cars / name).read_bytes(),
                                 (PREPARE["BUNDLED_CARS"] / name).read_bytes())
            tracks = destination / "PORT/TRACKS"
            self.assertEqual({path.name for path in tracks.iterdir()}, {"README.TXT"})
            self.assertEqual((tracks / "README.TXT").read_bytes(), PREPARE["TRACKS_README"].read_bytes())

    def test_private_package_preserves_loose_and_folder_car_overrides(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            data = directory / "game"
            data.mkdir()
            (data / "misc.res").write_bytes(b"version resource")
            (data / "cartest.res").write_bytes(b"loose override")
            (data / "cars").mkdir()
            (data / "cars/cartest.res").write_bytes(b"folder fallback")
            (data / "cars/sttest.p3s").write_bytes(b"packed car model")
            (data / "cars/stdatest.xvs").write_bytes(b"alternate packed dashboard")
            (data / "cars/readme.txt").write_text("not a game resource")
            destination = directory / "private"
            PREPARE["prepare"](data, destination)
            self.assertEqual((destination / "DATA/CARTEST.RES").read_bytes(), b"loose override")
            self.assertEqual((destination / "DATA/CARS/CARTEST.RES").read_bytes(),
                             b"folder fallback")
            self.assertEqual({path.name for path in (destination / "DATA/CARS").iterdir()},
                             {"CARTEST.RES", "STTEST.P3S", "STDATEST.XVS"})
            self.assertEqual({entry["name"] for directory in DATA_CD["collect_data"](data)
                              if directory["name"] == "CARS" for entry in directory["children"]},
                             {"CARTEST.RES", "STTEST.P3S", "STDATEST.XVS"})

    def test_private_redsea_image_preserves_car_subdirectory_and_parent(self):
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary)
            (data / "MISC.RES").write_bytes(b"version resource")
            (data / "CARS").mkdir()
            (data / "CARS/CARTEST.RES").write_bytes(b"custom car")
            assets = DATA_CD["collect_data"](data)
            helper = {"name": "IMPORT.HC", "data": b"helper", "date": 1}
            image = DATA_CD["build_image"]([helper], assets)
            boot = DATA_CD["REDSEA_OFFSET"] * DATA_CD["SECTOR_BYTES"]
            root_cluster = struct.unpack_from("<5Q", image, boot + DATA_CD["BOOT_FIELDS_OFFSET"])[2]
            root = directory_entries(image, root_cluster, DATA_CD["SECTOR_BYTES"])
            _, data_cluster, data_size = root["DATA"]
            data_entries = directory_entries(image, data_cluster, data_size)
            _, cars_cluster, cars_size = data_entries["CARS"]
            cars = directory_entries(image, cars_cluster, cars_size)
            self.assertEqual(cars[".."][1], data_cluster)
            self.assertEqual(set(cars), {"CARS", "..", "CARTEST.RES"})
            _, car_cluster, car_size = cars["CARTEST.RES"]
            offset = car_cluster * DATA_CD["SECTOR_BYTES"]
            self.assertEqual(image[offset:offset + car_size], b"custom car")

    def test_private_packages_preserve_track_folder_and_loose_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            data = directory / "game"
            tracks = data / "Tracks"
            tracks.mkdir(parents=True)
            (data / "MISC.RES").write_bytes(b"version resource")
            (data / "custom.trk").write_bytes(b"loose override")
            (tracks / "Custom.TrK").write_bytes(b"folder track")
            (tracks / "custom.hig").write_bytes(b"high scores")
            (tracks / "ignore.res").write_bytes(b"not a track")
            (tracks / "README.TXT").write_text("track instructions")
            (tracks / "nested").mkdir()
            (tracks / "nested/ignored.trk").write_bytes(b"nested track")
            destination = directory / "private"
            PREPARE["prepare"](data, destination)
            self.assertEqual((destination / "DATA/CUSTOM.TRK").read_bytes(), b"loose override")
            self.assertEqual({path.name for path in (destination / "DATA/TRACKS").iterdir()},
                             {"CUSTOM.TRK", "CUSTOM.HIG"})
            self.assertEqual((destination / "DATA/TRACKS/CUSTOM.TRK").read_bytes(), b"folder track")
            assets = DATA_CD["collect_data"](data)
            helper = {"name": "IMPORT.HC", "data": b"helper", "date": 1}
            image = DATA_CD["build_image"]([helper], assets)
            boot = DATA_CD["REDSEA_OFFSET"] * DATA_CD["SECTOR_BYTES"]
            root_cluster = struct.unpack_from("<5Q", image, boot + DATA_CD["BOOT_FIELDS_OFFSET"])[2]
            root = directory_entries(image, root_cluster, DATA_CD["SECTOR_BYTES"])
            _, data_cluster, data_size = root["DATA"]
            entries = directory_entries(image, data_cluster, data_size)
            _, tracks_cluster, tracks_size = entries["TRACKS"]
            entries = directory_entries(image, tracks_cluster, tracks_size)
            self.assertEqual(entries[".."][1], data_cluster)
            self.assertEqual(set(entries), {"TRACKS", "..", "CUSTOM.TRK", "CUSTOM.HIG"})
            _, track_cluster, track_size = entries["CUSTOM.TRK"]
            offset = track_cluster * DATA_CD["SECTOR_BYTES"]
            self.assertEqual(image[offset:offset + track_size], b"folder track")

    def test_case_collisions_in_track_folders_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            data = directory / "game"
            tracks = data / "tracks"
            tracks.mkdir(parents=True)
            (data / "MISC.RES").write_bytes(b"version resource")
            (tracks / "CUSTOM.TRK").write_bytes(b"first")
            (tracks / "custom.trk").write_bytes(b"second")
            with self.assertRaisesRegex(ValueError, "same uppercase name"):
                PREPARE["prepare"](data, directory / "package")
            with self.assertRaisesRegex(ValueError, "same uppercase name"):
                DATA_CD["collect_data"](data)

    def test_case_collisions_in_car_folders_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            data = directory / "game"
            data.mkdir()
            (data / "MISC.RES").write_bytes(b"version resource")
            (data / "cars").mkdir()
            (data / "cars/CARTEST.RES").write_bytes(b"first")
            (data / "cars/cartest.res").write_bytes(b"second")
            with self.assertRaisesRegex(ValueError, "same uppercase name"):
                PREPARE["prepare"](data, directory / "package")
            with self.assertRaisesRegex(ValueError, "same uppercase name"):
                DATA_CD["collect_data"](data)

    def test_flat_data_image_preserves_multisector_directories_and_empty_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary)
            contents = {f"FILE{index:02}.RES": bytes([index]) * index for index in range(20)}
            contents["MISC.RES"] = b"version resource"
            for name, content in contents.items():
                (data / name).write_bytes(content)
            helper = {"name": "IMPORT.HC", "data": b"helper", "date": 1}
            image = DATA_CD["build_image"]([helper], DATA_CD["collect_data"](data))
            boot = DATA_CD["REDSEA_OFFSET"] * DATA_CD["SECTOR_BYTES"]
            root_cluster = struct.unpack_from("<5Q", image, boot + DATA_CD["BOOT_FIELDS_OFFSET"])[2]
            root = directory_entries(image, root_cluster, DATA_CD["SECTOR_BYTES"])
            _, data_cluster, data_size = root["DATA"]
            self.assertGreater(data_size, DATA_CD["SECTOR_BYTES"])
            entries = directory_entries(image, data_cluster, data_size)
            self.assertEqual(set(entries) - {"DATA", ".."}, set(contents))
            for name, expected in contents.items():
                _, cluster, size = entries[name]
                if not expected:
                    self.assertEqual((cluster, size), (DATA_CD["INVALID_CLUSTER"], 0))
                else:
                    offset = cluster * DATA_CD["SECTOR_BYTES"]
                    self.assertEqual(image[offset:offset + size], expected)


if __name__ == "__main__":
    unittest.main()
