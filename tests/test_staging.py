"""Check the CI packaging allowlist with synthetic executables and notices."""
import contextlib
import hashlib
import importlib.util
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("stage_build", TOOLS / "stage_build.py")
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


class StagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.output = self.root / "staging"
        sources = [self.build / "deadspace", self.build / "deadspace.exe", self.root / "README.md",
                   self.root / "docs/DEVELOPMENT.md", self.root / "docs/licenses/Boost-1.0.txt"]
        sources += [self.root / "external/dynarmic" / name for name in staging.BUNDLED_NOTICES]
        sources += [self.root / "mingw/share/licenses" / name / "LICENSE" for name in staging.MINGW_NOTICES]
        for path in sources:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("synthetic test input", encoding="utf-8")

    def stage(self, platform="linux-x86_64", prefix=True):
        args = ["stage_build.py", "--platform", platform, "--build-dir", str(self.build), "--output", str(self.output)]
        if prefix:
            args += ["--mingw-prefix", str(self.root / "mingw")]
        with patch.object(staging, "ROOT", self.root), patch.object(sys, "argv", args), \
                patch("subprocess.check_output", return_value="synthetic-revision\n"), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            staging.main()

    def test_allowlist_excludes_private_build_files(self):
        (self.build / "private.apk").write_text("synthetic", encoding="utf-8")
        (self.build / "gamedata").mkdir()
        (self.build / "gamedata/private.txt").write_text("synthetic", encoding="utf-8")
        self.stage()
        self.assertFalse((self.output / "private.apk").exists())
        self.assertFalse((self.output / "gamedata").exists())
        self.assertTrue((self.output / "licenses/dynarmic.txt").is_file())
        for line in (self.output / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(digest, hashlib.sha256((self.output / name).read_bytes()).hexdigest())

    def test_windows_notices(self):
        self.stage("windows-x86_64")
        self.assertTrue((self.output / "deadspace.exe").is_file())
        self.assertTrue((self.output / "licenses/SDL2/LICENSE").is_file())
        self.assertFalse((self.output / "deadspace").exists())

    def test_existing_output_is_never_deleted(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("keep", encoding="utf-8")
        with self.assertRaises(SystemExit):
            self.stage()
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep")

    def test_missing_binary_fails_before_staging(self):
        (self.build / "deadspace").unlink()
        with self.assertRaises(SystemExit):
            self.stage()
        self.assertFalse(self.output.exists())

    def test_renamed_guest_code_is_rejected(self):
        (self.build / "deadspace").write_bytes(b"dex\n035\0synthetic")
        with self.assertRaises(SystemExit) as result:
            self.stage()
        self.assertEqual(result.exception.code, 1)

    def test_windows_requires_dependency_notices(self):
        with self.assertRaises(SystemExit):
            self.stage("windows-x86_64", prefix=False)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
