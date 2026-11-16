"""Mock-only recorder tests. Never launch WPR, elevate, sample or run the game."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("capture_gameplay_cpu", ROOT / "tools/capture_gameplay_cpu.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class Backend:
    def __init__(self, exe):
        self.calls = []
        self.admin = True
        self.process_list = [{"pid": 123, "exe": str(exe), "start_utc": "synthetic"}]
        self.status = "WPR is not recording"
        self.fail_start = False
        self.fail_stop = False
        self.interrupt = False
        self.trace = True
        self.waited = []

    def is_admin(self):
        return self.admin

    def processes(self):
        return self.process_list

    def run(self, args):
        self.calls.append(args)
        code, output = 0, "synthetic successful WPR output"
        if args[0] == "-status":
            output = self.status
        elif args[0] == "-start" and self.fail_start:
            code = 1
        elif args[0] == "-stop":
            if self.fail_stop:
                code = 1
            elif self.trace:
                Path(args[1]).write_bytes(b"synthetic ETL placeholder, not a real trace")
        return subprocess.CompletedProcess(args, code, output, "")

    def sleep(self, seconds):
        self.waited.append(seconds)
        if self.interrupt:
            raise KeyboardInterrupt()


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="deadspace-cpu-synthetic-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.exe = self.root / "synthetic-port.exe"
        self.exe.write_bytes(b"NOT executable code; synthetic fixture")
        self.output = self.root / "new capture"
        self.backend = Backend(self.exe)
        self.redirect = contextlib.ExitStack()
        self.addCleanup(self.redirect.close)
        self.redirect.enter_context(contextlib.redirect_stdout(io.StringIO()))
        self.redirect.enter_context(contextlib.redirect_stderr(io.StringIO()))

    def run_capture(self, seconds=60):
        return capture.capture(self.exe, self.output, seconds, self.backend)

    def state(self):
        return json.loads((self.output / "capture-state.json").read_text())

    def test_success_bounded_and_named_only(self):
        trace = self.run_capture()
        self.assertTrue(trace.is_file())
        self.assertEqual(self.backend.waited, [60])
        self.assertEqual([x[0] for x in self.backend.calls], ["-status", "-start", "-stop"])
        start, stop = self.backend.calls[1:]
        self.assertEqual(start[-2], "-instancename")
        self.assertEqual(start[-2:], stop[-2:])
        self.assertRegex(start[-1], r"^DeadSpaceCpu-[0-9a-f]{32}$")
        self.assertNotIn("-filemode", start)
        self.assertNotIn("-cancel", str(self.backend.calls))
        self.assertEqual(self.state()["phase"], "stopped")
        self.assertEqual(self.state()["process"]["pid"], 123)
        self.assertEqual(self.exe.read_bytes(), b"NOT executable code; synthetic fixture")

    def test_refuse_without_admin_before_process_or_wpr_work(self):
        self.backend.admin = False
        with self.assertRaises(ValueError):
            self.run_capture()
        self.assertFalse(self.output.exists())
        self.assertEqual(self.backend.calls, [])

    def test_refuse_missing_wrong_or_multiple_game_without_control(self):
        for processes in ([], [{"exe": str(self.root / "other.exe")}], self.backend.process_list * 2):
            self.backend.process_list = processes
            with self.assertRaises(ValueError):
                self.run_capture()
            self.assertFalse(self.output.exists())
        self.assertEqual(self.backend.calls, [])

    def test_existing_output_preserved(self):
        self.output.mkdir()
        marker = self.output / "keep.txt"
        marker.write_text("keep")
        with self.assertRaises(ValueError):
            self.run_capture()
        self.assertEqual(marker.read_text(), "keep")
        self.assertEqual(self.backend.calls, [])

    def test_existing_etl_never_overwritten_or_session_stopped(self):
        self.run_capture()
        before = self.backend.calls[:]
        state = self.state(); state["phase"] = "recording"
        capture.save(self.output, state)
        with self.assertRaises(ValueError):
            capture.finish(self.output, self.backend)
        self.assertEqual(self.backend.calls, before)

    def test_active_or_unknown_recorder_is_not_cancelled(self):
        for status in ("WPR recording active", "localized/unknown status", ""):
            self.backend.status = status
            with self.assertRaises(ValueError):
                self.run_capture()
            self.assertFalse(self.output.exists())
        self.assertTrue(all(call == ["-status"] for call in self.backend.calls))

    def test_failed_start_does_not_stop_or_wait(self):
        self.backend.fail_start = True
        with self.assertRaises(ValueError):
            self.run_capture()
        self.assertEqual(self.state()["phase"], "start-failed")
        self.assertEqual([x[0] for x in self.backend.calls], ["-status", "-start"])
        self.assertEqual(self.backend.waited, [])

    def test_interrupt_stops_own_recording_without_stopping_game(self):
        self.backend.interrupt = True
        with self.assertRaises(KeyboardInterrupt):
            self.run_capture()
        self.assertEqual(self.state()["phase"], "stopped")
        self.assertEqual(self.backend.calls[-1][0], "-stop")

    def test_failed_stop_recover_only_same_unique_instance(self):
        self.backend.fail_stop = True
        with self.assertRaises(ValueError):
            self.run_capture()
        state = self.state()
        self.assertEqual(state["phase"], "stop-failed")
        self.backend.fail_stop = False
        capture.finish(self.output, self.backend)
        self.assertEqual(self.backend.calls[-1][-2:], ["-instancename", state["instance"]])
        self.assertEqual(self.state()["phase"], "stopped")

    def test_ambiguous_start_allows_explicit_named_recovery_only(self):
        self.backend.fail_start = True
        with self.assertRaises(ValueError):
            self.run_capture()
        self.assertFalse(any(x[0] == "-stop" for x in self.backend.calls))
        instance = self.state()["instance"]
        capture.finish(self.output, self.backend)
        self.assertEqual(self.backend.calls[-1][-2:], ["-instancename", instance])

    def test_no_trace_is_not_reported_as_success(self):
        self.backend.trace = False
        with self.assertRaises(ValueError):
            self.run_capture()
        self.assertEqual(self.state()["phase"], "stopped-without-trace")

    def test_wait_exception_stops_own_trace(self):
        with patch.object(self.backend, "sleep", side_effect=OSError("synthetic interrupted wait")):
            with self.assertRaises(OSError):
                self.run_capture()
        self.assertEqual(self.state()["phase"], "stopped")

    def test_start_timeout_keeps_unique_metadata_without_stopping_any_session(self):
        run = self.backend.run
        def timeout(args):
            if args[0] == "-start":
                raise subprocess.TimeoutExpired("synthetic-wpr", 180)
            return run(args)
        with patch.object(self.backend, "run", side_effect=timeout):
            with self.assertRaises(subprocess.TimeoutExpired):
                self.run_capture()
        self.assertEqual(self.state()["phase"], "start-pending")
        self.assertFalse(any(x[0] == "-stop" for x in self.backend.calls))
        capture.finish(self.output, self.backend)
        self.assertEqual(self.state()["phase"], "stopped")

    def test_metadata_failure_after_start_does_not_leave_trace_running(self):
        save = capture.save
        writes = 0
        def fail_after_first(*args):
            nonlocal writes
            writes += 1
            if writes > 1:
                raise OSError("synthetic metadata storage failure")
            save(*args)
        with patch.object(capture, "save", side_effect=fail_after_first):
            with self.assertRaises(OSError):
                self.run_capture()
        self.assertEqual(self.backend.calls[-1][0], "-stop")
        self.assertTrue((self.output / "cpu.etl").exists())

    def test_forged_or_finished_recovery_state_refused(self):
        self.run_capture()
        before = len(self.backend.calls)
        original = self.state()
        for change in ({"instance": "WprApp"}, {"instance": "DeadSpaceCpu-../other"},
                       {"tool": "someone-else"}, {"directory": str(self.root)}, {"phase": "stopped"}):
            state = dict(original, phase="recording");state.update(change)
            capture.save(self.output, state)
            with self.assertRaises(ValueError):
                capture.finish(self.output, self.backend)
        self.assertEqual(len(self.backend.calls), before)

    def test_duration_is_bounded_before_recording(self):
        for seconds in (0, 14, 121, 999999):
            with self.assertRaises(ValueError):
                self.run_capture(seconds)
        self.assertEqual(self.backend.calls, [])
        self.assertFalse(self.output.exists())

    def test_profile_is_cpu_only_and_bounded_memory_mode(self):
        profile = ET.parse(capture.PROFILE).getroot()
        keywords = {p.attrib["Value"] for p in profile.findall(".//Keyword")}
        self.assertEqual(keywords, {"CpuConfig", "ProcessThread", "Loader", "SampledProfile"})
        self.assertEqual([p.attrib["Value"] for p in profile.findall(".//Stack")], ["SampledProfile"])
        self.assertEqual(len(profile.findall(".//EventProvider")), 0)
        self.assertEqual(profile.find(".//Profile").attrib["LoggingMode"], "Memory")
        self.assertEqual(int(profile.find(".//BufferSize").attrib["Value"]) *
                         int(profile.find(".//Buffers").attrib["Value"]), 64 * 1024)


if __name__ == "__main__":
    unittest.main()
