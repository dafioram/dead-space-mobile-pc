"""Synthetic policy tests; no game data or binary fixtures are stored in Git."""
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "tools" / "check_assets.py"
spec = importlib.util.spec_from_file_location("check_assets", CHECKER)
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class AssetPolicyTests(unittest.TestCase):
    def test_private_paths(self):
        for name in ("Game.APK", "x/game.obb", "gamedata/file", "assets/file", "x/userdata/save",
                     "mods/textures/_dump/123.png", "texture.m3g", "data.sbin", "libDeadSpace.so",
                     "gamedata\\file", "../outside", "C:/outside"):
            with self.subTest(name=name):
                self.assertIsNotNone(policy.path_problem(name))

    def test_source_and_port_binary_paths(self):
        for name in ("src/apk.cpp", "src/asset_file.h", "tests/test_apk.cpp", "deadspace.exe", "deadspace"):
            self.assertIsNone(policy.path_problem(name))

    def test_renamed_guest_library(self):
        elf = bytearray(20)
        elf[:6] = b"\x7fELF\x01\x01"
        elf[18] = 40
        self.assertIsNotNone(policy.content_problem(io.BytesIO(elf)))
        elf[4] = 2  # A host ELF64 executable is not the guest library.
        self.assertIsNone(policy.content_problem(io.BytesIO(elf)))

    def test_archive_contents(self):
        for name, rejected in (("assets/private.txt", True), ("classes.dex", True), ("deadspace.exe", False)):
            stream = io.BytesIO()
            with zipfile.ZipFile(stream, "w") as archive:
                archive.writestr(name, b"synthetic test content")
            stream.seek(0)
            self.assertEqual(policy.content_problem(stream) is not None, rejected)

    def test_nested_archive(self):
        inner = io.BytesIO()
        with zipfile.ZipFile(inner, "w") as archive:
            archive.writestr("assets/private.txt", "synthetic")
        outer = io.BytesIO()
        with zipfile.ZipFile(outer, "w") as archive:
            archive.writestr("renamed.zip", inner.getvalue())
        outer.seek(0)
        self.assertIsNotNone(policy.content_problem(outer))

    def test_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "deadspace.exe").write_bytes(b"synthetic host executable")
            self.assertEqual(policy.check_package(root), [])
            (root / "private.apk").write_bytes(b"synthetic")
            self.assertTrue(policy.check_package(root))

    def test_checks_index_not_working_tree(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            payload = root / "renamed.bin"
            payload.write_bytes(b"dex\n035\0synthetic")
            subprocess.run(["git", "add", "renamed.bin"], cwd=root, check=True)
            payload.write_text("innocent unstaged replacement", encoding="utf-8")
            result = subprocess.run([sys.executable, str(CHECKER), "--staged"], cwd=root, capture_output=True)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn(b"DEX", result.stderr)

    def test_tracked_check_on_clean_index(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "-q", directory], check=True)
            (root / "private.apk").write_bytes(b"synthetic")
            subprocess.run(["git", "add", "private.apk"], cwd=root, check=True)
            subprocess.run(["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                            "commit", "-qm", "synthetic fixture"], cwd=root, check=True)
            staged = subprocess.run([sys.executable, str(CHECKER), "--staged"], cwd=root, capture_output=True)
            tracked = subprocess.run([sys.executable, str(CHECKER), "--tracked"], cwd=root, capture_output=True)
            self.assertEqual(staged.returncode, 0)
            self.assertEqual(tracked.returncode, 1)


if __name__ == "__main__":
    unittest.main()
