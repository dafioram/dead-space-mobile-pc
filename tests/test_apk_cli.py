"""Only import-only/argument-error paths of the real EXE. Never game execution."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
from test_apk_import import arm_header


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="deadspace-apk-cli-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.apk = self.root / "fixture.apk"
        self.dest = self.root / "game"

    def archive(self, reanimated=False):
        with zipfile.ZipFile(self.apk, "w") as z:
            z.writestr("assets/game/libDeadSpace32.so" if reanimated else "lib/armeabi/libDeadSpace.so", arm_header())
            z.writestr("assets/published/sample", b"synthetic")

    def run_cli(self, args):
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        return subprocess.run([EXE, *map(str, args)], cwd=self.root, env=env, capture_output=True, timeout=15)

    def test_import_only_before_profile_and_debug_initialization(self):
        self.archive()
        result = self.run_cli(["--apk", self.apk, "--game-dir", self.dest, "--import-only"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(b"no SDL", result.stdout)
        self.assertEqual((self.dest / "libDeadSpace.so").read_bytes(), arm_header())
        self.assertEqual({p.name for p in self.root.iterdir()}, {"fixture.apk", "game"})
        self.assertFalse((self.dest / "userdata").exists())

    def test_import_only_cannot_activate_crash_or_audio_flags(self):
        self.archive()
        for flag in ("--crash-test", "--dump-audio", "--exit-frame", "--data"):
            result = self.run_cli(["--import-only", "--apk", self.apk, "--game-dir", self.dest, flag])
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertFalse(self.dest.exists())

    def test_help_still_wins_over_import_and_crash(self):
        result = self.run_cli(["--import-only", "--apk", self.apk, "--game-dir", self.dest, "--crash-test", "--help"])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(b"--game-dir", result.stdout)
        self.assertEqual(list(self.root.iterdir()), [])

    def test_reanimated_refused_before_destination_writes(self):
        self.archive(reanimated=True)
        result = self.run_cli(["--apk", self.apk, "--game-dir", self.root / "new-parent/game", "--import-only"])
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn(b"Reanimated", result.stdout)
        self.assertFalse((self.root / "new-parent").exists())

    def test_normal_file_argument_conflicts_exit_early(self):
        for args in (["--game-dir"], ["--apk"], ["--game-dir", self.dest, "--assets", "elsewhere"],
                     ["--apk", self.apk, "--lib", "elsewhere"]):
            result = self.run_cli(args)
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        self.assertEqual(list(self.root.iterdir()), [])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    EXE = str(args.executable.resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
