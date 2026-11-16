"""Synthetic helper processes only: profile locks and interrupted transactions.

Never starts the application/JIT or uses a real userdata directory.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

PROBE = None


class SaveProcessTests(unittest.TestCase):
    def start_lock(self, root):
        process = subprocess.Popen([str(PROBE), "--lock", str(root)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop, process)
        self.assertEqual(process.stdout.readline().strip(), "LOCKED")
        return process

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=15)

    def probe_lock(self, root):
        return subprocess.run([str(PROBE), "--lock", str(root)], input="\n", text=True,
                              capture_output=True, timeout=15).returncode

    def test_separate_process_writer_exclusion_and_release(self):
        with tempfile.TemporaryDirectory(prefix="deadspace-lock-test-") as name:
            root = Path(name)
            first = self.start_lock(root)
            self.assertEqual(self.probe_lock(root), 2)
            first.communicate(input="\n", timeout=15)
            self.assertEqual(first.returncode, 0)
            self.assertEqual(self.probe_lock(root), 0)

    def test_killed_process_does_not_leave_a_stale_lock(self):
        with tempfile.TemporaryDirectory(prefix="deadspace-lock-kill-test-") as name:
            root = Path(name)
            first = self.start_lock(root)
            first.kill()
            first.communicate(timeout=15)
            self.assertEqual(self.probe_lock(root), 0)

    def test_termination_after_stage_write_preserves_committed_final(self):
        with tempfile.TemporaryDirectory(prefix="deadspace-save-kill-test-") as name:
            root = Path(name)
            result = subprocess.run([str(PROBE), "--abandon", str(root)], capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            final = root / "00_LevelSave.sb"
            original = final.read_bytes()
            temporary = list(root.glob(".ds-save-*.tmp"))
            self.assertEqual(len(temporary), 1)
            self.assertNotEqual(temporary[0].read_bytes(), original)
            self.assertEqual(self.probe_lock(root), 0)
            self.assertEqual(final.read_bytes(), original)
            self.assertTrue(temporary[0].exists(), "startup must not silently promote/delete abandoned output")
            self.assertFalse((root / "00_LevelSave.sb.bak").exists())

    def test_termination_at_commit_boundaries(self):
        for step in ("sync", "close", "backup", "replace"):
            with self.subTest(stage=step), tempfile.TemporaryDirectory(prefix="deadspace-save-stage-test-") as name:
                root = Path(name)
                result = subprocess.run([str(PROBE), "--terminate-at", step, str(root)], capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                final = root / "00_LevelSave.sb"
                validation = subprocess.run([str(PROBE), "--inspect", str(final)], capture_output=True, timeout=15)
                self.assertEqual(validation.returncode, 0, validation.stdout)
                backup = root / "00_LevelSave.sb.bak"
                if step in ("backup", "replace"):
                    self.assertTrue(backup.exists())
                    self.assertEqual(final.read_bytes() == backup.read_bytes(), step == "backup")
                else:
                    self.assertFalse(backup.exists())
                self.assertEqual(len(list(root.glob(".ds-save-*.tmp"))), 0 if step == "replace" else 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    PROBE = args.probe.resolve()
    unittest.main(argv=[__file__] + remaining)
