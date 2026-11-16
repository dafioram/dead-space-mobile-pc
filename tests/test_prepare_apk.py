"""Preparation helper executes only the asset-free probe's --import-only path."""
import argparse
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile
from test_apk_import import arm_header

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare", ROOT / "tools/prepare_apk_test.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class PrepareTests(unittest.TestCase):
    def test_separate_cases_never_launch_or_touch_original_profile(self):
        with tempfile.TemporaryDirectory(prefix="deadspace-prepare-") as name:
            root = Path(name)
            apk = root / "fixture.apk"
            with zipfile.ZipFile(apk, "w") as z:
                z.writestr("lib/armeabi/libDeadSpace.so", arm_header())
                z.writestr("assets/published/sample", "synthetic")
            original = root / "original-userdata"
            original.mkdir()
            save = original / "keep.sb"
            save.write_bytes(b"synthetic save")
            before = (save.read_bytes(), save.stat().st_mtime_ns)
            for n in ("A", "B"):
                case = prepare.prepare(Path(PROBE), apk, root / n)
                self.assertTrue((case / "gamedata/assets/published/sample").is_file())
                self.assertFalse((case / "userdata").exists(), "helper started gameplay/created profile")
                self.assertFalse((case / "deadspace.log").exists())
                self.assertTrue((case / "import.log").is_file())
                self.assertTrue((case / ("Launch.cmd" if Path(PROBE).suffix.lower() == ".exe" else "Launch.sh")).is_file())
            with self.assertRaises(FileExistsError):
                prepare.prepare(Path(PROBE), apk, root / "A")
            self.assertEqual(before, (save.read_bytes(), save.stat().st_mtime_ns))

    def test_failed_package_gets_no_launcher_or_partial_data(self):
        with tempfile.TemporaryDirectory(prefix="deadspace-prepare-fail-") as name:
            root = Path(name)
            apk = root / "fixture.apk"
            with zipfile.ZipFile(apk, "w") as z:
                z.writestr("assets/game/libDeadSpace32.so", arm_header())
                z.writestr("assets/published/sample", "synthetic")
            with self.assertRaises(ValueError):
                prepare.prepare(Path(PROBE), apk, root / "case")
            self.assertFalse((root / "case/gamedata").exists())
            self.assertFalse(list((root / "case").glob("Launch.*")))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    PROBE = str(args.probe.resolve())
    unittest.main(argv=[__file__, *remaining], verbosity=2)
