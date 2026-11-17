"""Exercise the production importer using generated ZIPs and a non-executable ELF header."""
import argparse
import hashlib
import io
import random
import shutil
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile


def arm_header():
    data = bytearray(64)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H", data, 16, 3)
    struct.pack_into("<H", data, 18, 40)
    struct.pack_into("<I", data, 20, 1)
    struct.pack_into("<H", data, 40, 52)
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

    def run_import(self, success, fault=None):
        args = [IMPORTER, str(self.apk), str(self.dest)]
        if fault:
            args.append(fault)
        result = subprocess.run(args, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        self.assertFalse(list(self.root.glob(".deadspace-import-*")), "handled import leaked its own stage")
        return result

    def test_supported_layouts_and_compression(self):
        for library in ("assets/guest_libs/libDeadSpace.so", "lib/armeabi/libDeadSpace.so",
                        "lib/armeabi-v7a/libDeadSpace.so"):
            for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                with self.subTest(library=library, method=method):
                    if self.dest.exists():
                        shutil.rmtree(self.dest)  # only this test's generated destination
                    self.write_archive(library, method=method)
                    self.run_import(True)
                    self.assertEqual((self.dest / "libDeadSpace.so").read_bytes(), arm_header())
                    self.assertEqual((self.dest / "assets/published/sample.txt").read_bytes(), b"synthetic asset")
                    self.assertEqual((self.dest / "assets/empty.txt").read_bytes(), b"")
                    self.assertEqual((self.dest / "atc_textures").exists(), library.startswith("lib/"))
                    self.assertFalse((self.dest / "classes.dex").exists())
                    self.assertFalse((self.dest / "assets/guest_libs").exists())

    def test_different_reimport_does_not_overwrite(self):
        self.write_archive("lib/armeabi/libDeadSpace.so")
        self.run_import(True)
        self.assertTrue((self.dest / "atc_textures").exists())
        self.write_archive()
        before = self.snapshot()
        self.run_import(False)
        self.assertEqual(before, self.snapshot())
        self.assertTrue((self.dest / "atc_textures").exists())

    def snapshot(self):
        return {p.relative_to(self.dest).as_posix(): (p.read_bytes(), p.stat().st_mtime_ns)
                for p in self.dest.rglob("*") if p.is_file()}

    def test_identical_reimport_reuses_without_writes(self):
        self.write_archive()
        self.run_import(True)
        before = self.snapshot()
        result = self.run_import(True)
        self.assertIn(b"REUSED", result.stdout)
        self.assertEqual(before, self.snapshot())

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

    def test_unsafe_paths_refuse_without_partial_extraction(self):
        for member in ("assets/../../escaped.txt", "assets/../escaped.txt", "assets/..\\escaped.txt",
                       "assets/C:/escaped.txt", "assets/nul.txt", "assets/a/./x", "assets/a//x", "assets/trailing./x",
                       "assets/a*/x", "assets/a?/x", "assets/a|/x", "assets/a\"/x"):
            with self.subTest(member=member):
                self.write_archive()
                with zipfile.ZipFile(self.apk, "a") as archive:
                    archive.writestr(member, "synthetic")
                self.run_import(False)
                self.assertFalse(self.dest.exists())
                self.assertFalse((self.root / "escaped.txt").exists())

    def test_reanimated_placeholder_and_arm64_explanation(self):
        self.write_archive("assets/game/libDeadSpace32.so")
        result = self.run_import(False)
        self.assertIn(b"Reanimated", result.stdout)
        self.assertFalse(self.dest.exists())
        self.write_archive("lib/arm64-v8a/libdsr.so", data=arm_header().replace(b"ELF\x01", b"ELF\x02"))
        result = self.run_import(False)
        self.assertIn(b"ARM64", result.stdout)
        self.assertFalse(self.dest.exists())

    def test_failures_preserve_existing_data(self):
        self.write_archive()
        self.run_import(True)
        (self.dest / "userdata").mkdir()
        (self.dest / "userdata/keep.sb").write_bytes(b"synthetic profile sentinel")
        before = self.snapshot()
        for fault in ("write", "close", "publish"):
            with self.subTest(fault=fault):
                # Publish fault needs a fresh target; existing equal data is never republished.
                if fault == "publish":
                    self.write_archive("lib/armeabi/libDeadSpace.so")
                self.run_import(False, fault)
                self.assertEqual(before, self.snapshot())

    def test_fresh_output_faults_do_not_publish(self):
        for fault in ("write", "close", "publish"):
            self.write_archive()
            self.run_import(False, fault)
            self.assertFalse(self.dest.exists())

    def test_symlink_entry_and_destination_refused(self):
        self.write_archive()
        entry = zipfile.ZipInfo("assets/published/link")
        entry.create_system = 3
        entry.external_attr = (0o120777 << 16)
        with zipfile.ZipFile(self.apk, "a") as archive:
            archive.writestr(entry, "outside")
        self.run_import(False)
        self.assertFalse(self.dest.exists())
        target = self.root / "target"
        target.mkdir()
        (target / "keep").write_text("sentinel")
        try:
            self.dest.symlink_to(target, target_is_directory=True)
        except OSError:
            return  # Windows may not permit unprivileged symlink creation.
        self.write_archive()
        self.run_import(False)
        self.assertEqual((target / "keep").read_text(), "sentinel")
        self.dest = self.dest / "new/game"
        self.run_import(False)
        self.assertFalse((target / "new").exists())

    def test_trailing_destination_separator(self):
        self.write_archive()
        result = subprocess.run([IMPORTER, str(self.apk), str(self.dest) + "/"], capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.dest / "libDeadSpace.so").is_file())

    def test_case_aliased_entries_refused(self):
        self.write_archive()
        with zipfile.ZipFile(self.apk, "a") as archive:
            archive.writestr("assets/published/SAMPLE.TXT", "different")
        self.run_import(False)
        self.assertFalse(self.dest.exists())

    def test_profile_files_excluded(self):
        self.write_archive()
        with zipfile.ZipFile(self.apk, "a") as archive:
            archive.writestr("assets/var/settings.sb", "synthetic save")
            archive.writestr("assets/userdata/keep", "synthetic profile")
        self.run_import(True)
        self.assertFalse((self.dest / "assets/var").exists())
        self.assertFalse((self.dest / "assets/userdata").exists())

    def test_stored_crc_failure_rolls_back(self):
        self.write_archive(method=zipfile.ZIP_STORED)
        with zipfile.ZipFile(self.apk) as archive:
            entry = archive.getinfo("assets/published/sample.txt")
            offset = entry.header_offset
        raw = bytearray(self.apk.read_bytes())
        name, extra = struct.unpack_from("<HH", raw, offset + 26)
        raw[offset + 30 + name + extra] ^= 1
        self.apk.write_bytes(raw)
        result = self.run_import(False)
        self.assertIn(b"CRC", result.stdout)
        self.assertFalse(self.dest.exists())

    def test_data_descriptor_zip(self):
        class Unseekable(io.BytesIO):
            def seekable(self):
                return False

            def seek(self, *args):
                raise io.UnsupportedOperation("synthetic streaming ZIP")

        output = Unseekable()
        with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("lib/armeabi/libDeadSpace.so", arm_header())
            archive.writestr("assets/published/sample.txt", "synthetic asset")
        self.apk.write_bytes(output.getvalue())
        self.run_import(True)
        self.assertEqual((self.dest / "assets/published/sample.txt").read_bytes(), b"synthetic asset")

    def test_selected_member_limits_and_metadata_errors(self):
        self.write_archive()
        valid = self.apk.read_bytes()
        central = valid.find(b"PK\x01\x02")
        local = 0
        # A declared oversized library must be refused before allocation/writes.
        for position, value, fmt in ((central + 24, 128 * 1024 * 1024 + 1, "I"),
                                     (local + 8, 0, "H"), (local + 14, 123, "I"),
                                     (local + 18, 123, "I")):
            raw = bytearray(valid)
            struct.pack_into("<" + fmt, raw, position, value)
            self.apk.write_bytes(raw)
            self.run_import(False)
            self.assertFalse(self.dest.exists())
        raw = bytearray(valid)
        # Match local/central encrypted flags, rather than only creating a mismatch.
        for position in (central + 8, local + 6):
            struct.pack_into("<H", raw, position, 1)
        self.apk.write_bytes(raw)
        self.run_import(False)
        self.assertFalse(self.dest.exists())

    def test_existing_empty_destination_and_additional_assets_are_not_replaced(self):
        self.write_archive()
        self.dest.mkdir()
        self.run_import(False)
        self.assertEqual(list(self.dest.iterdir()), [])
        self.dest = self.root / "fresh"
        self.run_import(True)
        (self.dest / "assets/keep.txt").write_bytes(b"synthetic user asset")
        before = self.snapshot()
        self.run_import(False)
        self.assertEqual(before, self.snapshot())

    def test_malformed_directory_bounds(self):
        self.write_archive()
        valid = self.apk.read_bytes()
        end = valid.rfind(b"PK\x05\x06")
        for position, value, fmt in ((end + 12, 0xffffffff, "I"), (end + 16, 0xfffffff0, "I"),
                                      (end + 10, 32000, "H"), (end + 4, 1, "H")):
            raw = bytearray(valid)
            struct.pack_into("<" + fmt, raw, position, value)
            self.apk.write_bytes(raw)
            self.run_import(False)
            self.assertFalse(self.dest.exists())

    def test_sha256_standard_vectors_and_boundaries(self):
        for data in (b"", b"abc", b"a" * 55, b"a" * 56, b"a" * 63, b"a" * 64, b"a" * 65,
                     b"a" * 1000000, bytes(range(256)) * 513):
            p = self.root / "hash-input"
            p.write_bytes(data)
            result = subprocess.run([IMPORTER, "--hash", str(p)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), hashlib.sha256(data).hexdigest())

    def test_unicode_paths_and_import_only_cli(self):
        self.apk = self.root / "game-\u00e9-\u51b7.apk"
        self.dest = self.root / "game-\u00e9-\u51b7"
        self.write_archive("lib/armeabi/libDeadSpace.so")
        result = subprocess.run([IMPORTER, "--apk", str(self.apk), "--game-dir", str(self.dest), "--import-only"],
                                cwd=self.root, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.dest / "assets/published/sample.txt").is_file())
        self.assertFalse((self.root / "userdata").exists())
        self.assertFalse((self.root / "deadspace.log").exists())

    def test_import_only_rejects_gameplay_options_and_missing_values(self):
        self.write_archive()
        for args in (["--import-only"], ["--import-only", "--apk"],
                     ["--import-only", "--apk", str(self.apk), "--game-dir"],
                     ["--import-only", "--apk", str(self.apk), "--game-dir", str(self.dest), "--dump-audio", "no.raw"],
                     ["--import-only", "--apk", str(self.apk), "--game-dir", str(self.dest), "--data", "no-profile"]):
            result = subprocess.run([IMPORTER, *args], cwd=self.root, capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertFalse(self.dest.exists())
        self.assertFalse((self.root / "no.raw").exists())

    def test_generated_directory_mutations_never_publish(self):
        self.write_archive()
        valid = self.apk.read_bytes()
        central = valid.find(b"PK\x01\x02")
        randomizer = random.Random(12345)
        for _ in range(80):
            raw = bytearray(valid)
            raw[central + randomizer.choice((0, 1, 2, 3, 28, 29, 30, 31, 42, 43, 44, 45))] ^= randomizer.randrange(1, 256)
            self.apk.write_bytes(raw)
            self.run_import(False)
            self.assertFalse(self.dest.exists())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--importer", required=True)
    args, remaining = parser.parse_known_args()
    IMPORTER = str(Path(args.importer).resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
