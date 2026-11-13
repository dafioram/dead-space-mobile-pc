"""Test smoke-test reporting without launching the game or reading private data."""
from pathlib import Path
import runpy
import subprocess
import sys
import unittest
from unittest.mock import mock_open, patch


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "starttest.py"


class StarttestExitTests(unittest.TestCase):
    def run_script(self, early_exit):
        with patch.object(sys, "argv", [str(SCRIPT), "1", "0.01"]), \
                patch("subprocess.Popen") as popen, \
                patch("shutil.rmtree"), patch("shutil.copy"), \
                patch("builtins.open", mock_open(read_data="synthetic log\n")), \
                patch("builtins.print"):
            process = popen.return_value
            process.wait.side_effect = [7] if early_exit else [subprocess.TimeoutExpired("synthetic", 0.01), 0]
            with self.assertRaises(SystemExit) as result:
                runpy.run_path(str(SCRIPT), run_name="__main__")
            return result.exception.code

    def test_early_exit_is_failure(self):
        self.assertEqual(self.run_script(True), 1)

    def test_surviving_until_timeout_is_success(self):
        self.assertEqual(self.run_script(False), 0)


if __name__ == "__main__":
    unittest.main()
