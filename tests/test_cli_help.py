#!/usr/bin/env python3
"""Run only the asset-free early help path, never the game or importer."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class HelpTests(unittest.TestCase):
    executable = None

    def test_help_exits_without_side_effects(self):
        for flag in ("--help", "-h"):
            with self.subTest(flag=flag), tempfile.TemporaryDirectory(prefix="deadspace-help-") as directory:
                root = Path(directory)
                # Help must win even over earlier side-effecting options.
                command = [str(self.executable), "--dump-audio", str(root / "audio.raw"),
                           "--log", str(root / "game.log"), "--data", str(root / "userdata"),
                           "--apk", str(root / "not-an-apk.zip"), "--no-save", flag]
                result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("Usage: deadspace [options]", result.stdout)
                self.assertIn("disposable", result.stdout)
                self.assertEqual(list(root.iterdir()), [], "help must not create logs, profiles or dumps")

    def test_listing_covers_recognized_flags(self):
        main = (Path(__file__).resolve().parents[1] / "src/main.cpp").read_text(encoding="utf-8")
        recognized = set(re.findall(r'a == "(--[a-z-]+|-v)"', main)) | {"--help", "-h"}
        result = subprocess.run([str(self.executable), "--help"], capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        documented = set(re.findall(r'(?<!\w)(--[a-z-]+|-[hv])(?!\w)', result.stdout))
        self.assertTrue(recognized <= documented, f"undocumented CLI flags: {recognized - documented}")

    def test_invalid_native_prep_exits_before_game_startup(self):
        for suffix in ([], ["unknown"]):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory(prefix="deadspace-native-prep-cli-") as directory:
                root = Path(directory)
                result = subprocess.run([str(self.executable), "--log", str(root / "game.log"),
                                         "--data", str(root / "userdata"), "--native-prep", *suffix],
                                        cwd=root, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("--native-prep requires optimized or reference", result.stderr)
                self.assertEqual(list(root.iterdir()), [], "invalid mode must exit before profiles/logs/window/game")

    def test_debug_guide_covers_command_names(self):
        root = Path(__file__).resolve().parents[1]
        guide = (root / "docs/debug_menu.md").read_text(encoding="utf-8")
        dev = (root / "src/devmode.cpp").read_text(encoding="utf-8")
        names = set(re.findall(r'c == "([a-z_]+)"', dev))
        missing = {name for name in names if not re.search(r'\b' + re.escape(name) + r'\b', guide)}
        self.assertFalse(missing, f"undocumented developer commands: {missing}")
        console = (root / "src/console.cpp").read_text(encoding="utf-8")
        names = set(re.findall(r'\{"(/[a-z_]+)"', console))
        self.assertTrue(all(name in guide for name in names), "document every visible slash command")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    HelpTests.executable = args.executable.resolve()
    unittest.main(argv=[__file__, *remaining])
