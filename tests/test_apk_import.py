"""Exercise the production importer using generated ZIPs and a non-executable ELF header."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile


def arm_header():
    data = bytearray(64)
    data[:6] = b"\x7fELF\x01\x01"
    struct.pack_into("<H", data, 18, 40)
    return bytes(data)


class ApkImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.apk = self.root / "synthetic.zip"
        self.dest = self.root / "imported"

    def write_archive(self, library="assets/guest_libs/libDeadSpace.so", data=None, method=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(self.apk, "w", compression=method) as archive:
            if library:
                archive.writestr(library, arm_header() if data is None else data)
            archive.writestr("assets/published/sample.txt", b"synthetic asset")
            archive.writestr("assets/empty.txt", b"")
            archive.writestr("assets/subdirectory/", b"")
            archive.writestr("classes.dex", b"ignored synthetic non-asset")

    def run_import(self, success):
        result = subprocess.run([IMPORTER, str(self.apk), str(self.dest)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)

    def test_supported_layouts_and_compression(self):
        for library in ("assets/guest_libs/libDeadSpace.so", "lib/armeabi/libDeadSpace.so",
                        "lib/armeabi-v7a/libDeadSpace.so"):
            for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                with self.subTest(library=library, method=method):
                    self.write_archive(library, method=method)
                    self.run_import(True)
                    self.assertEqual((self.dest / "libDeadSpace.so").read_bytes(), arm_header())
                    self.assertEqual((self.dest / "assets/published/sample.txt").read_bytes(), b"synthetic asset")
                    self.assertEqual((self.dest / "assets/empty.txt").read_bytes(), b"")
                    self.assertEqual((self.dest / "atc_textures").exists(), library.startswith("lib/"))
                    self.assertFalse((self.dest / "classes.dex").exists())
                    self.assertFalse((self.dest / "assets/guest_libs").exists())

    def test_reimport_updates_texture_mode(self):
        self.write_archive("lib/armeabi/libDeadSpace.so")
        self.run_import(True)
        self.assertTrue((self.dest / "atc_textures").exists())
        self.write_archive()
        self.run_import(True)
        self.assertFalse((self.dest / "atc_textures").exists())

    def test_prefer_guest_library(self):
        self.write_archive()
        with zipfile.ZipFile(self.apk, "a") as archive:
            archive.writestr("lib/armeabi/libDeadSpace.so", arm_header() + b"different synthetic header")
        self.run_import(True)
        self.assertEqual((self.dest / "libDeadSpace.so").read_bytes(), arm_header())
        self.assertFalse((self.dest / "atc_textures").exists())

    def test_no_library(self):
        self.write_archive(library=None)
        self.run_import(False)
        self.assertFalse(self.dest.exists())

    def test_invalid_library(self):
        for data in (b"", b"not ELF", arm_header()[:20], arm_header().replace(b"\x7fELF\x01", b"\x7fELF\x02")):
            with self.subTest(data=data):
                self.write_archive(data=data)
                self.run_import(False)

    def test_missing_archive(self):
        self.run_import(False)

    def test_invalid_zip(self):
        for data in (b"", b"not a ZIP", b"PK\x03\x04" + bytes(30)):
            with self.subTest(data=data):
                self.apk.write_bytes(data)
                self.run_import(False)

    def test_truncated_zip(self):
        self.write_archive()
        self.apk.write_bytes(self.apk.read_bytes()[:-22])
        self.run_import(False)

    def test_unsupported_compression(self):
        self.write_archive(method=zipfile.ZIP_BZIP2)
        self.run_import(False)

    def test_parent_traversal_is_skipped(self):
        self.write_archive()
        with zipfile.ZipFile(self.apk, "a") as archive:
            archive.writestr("assets/../../escaped.txt", "synthetic")
        self.run_import(True)
        self.assertFalse((self.root / "escaped.txt").exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--importer", required=True)
    args, remaining = parser.parse_known_args()
    IMPORTER = str(Path(args.importer).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
