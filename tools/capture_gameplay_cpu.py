#!/usr/bin/env python3
"""User-run Windows CPU sampling. NEVER launch, drive, suspend or stop the game.

The trace is system-wide and PRIVATE. No heap/dump/screenshot/file/network provider.
Only a uniquely named WPR instance requested here may be recovered/stopped here.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid


TOOL_ID = "deadspace-gameplay-cpu-v1"
PROFILE = Path(__file__).with_name("gameplay_cpu.wprp")


class WindowsBackend:
    def __init__(self):
        if os.name != "nt":
            raise ValueError("Windows-only capture; no recording was started")
        self.wpr = str(Path(os.environ["SystemRoot"]) / "System32/wpr.exe")

    def is_admin(self):
        return bool(ctypes.windll.shell32.IsUserAnAdmin())

    def run(self, args):
        return subprocess.run([self.wpr, *args], capture_output=True, text=True,
                              errors="replace", timeout=180)

    def processes(self):
        # Read-only process metadata. Never obtain a game write/suspend handle.
        script = ("$ErrorActionPreference='Stop'; "
                  "@(Get-Process -Name deadspace -ErrorAction SilentlyContinue | "
                  "ForEach-Object { [pscustomobject]@{pid=$_.Id; exe=$_.Path; "
                  "start_utc=$_.StartTime.ToUniversalTime().ToString('o')} }) | ConvertTo-Json -Compress")
        result = subprocess.run(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                                capture_output=True, text=True, errors="replace", timeout=30, check=True)
        value = json.loads(result.stdout) if result.stdout.strip() else []
        return value if isinstance(value, list) else [value]

    def sleep(self, seconds):
        time.sleep(seconds)


def utc():
    return datetime.now(timezone.utc).isoformat()


def save(directory, state):
    # Directory is newly created by capture, or explicitly supplied for recovery.
    # Replace only this helper's own metadata, never an ETL or arbitrary user file.
    temporary = directory / ("capture-state-" + uuid.uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(state, stream, indent=2)
        stream.write("\n")
    temporary.replace(directory / "capture-state.json")


def record_command(directory, name, result):
    with (directory / name).open("x", encoding="utf-8") as stream:
        stream.write(f"exit={result.returncode}\n{result.stdout}\n{result.stderr}\n")


def remember(directory, state):
    # A full/unwritable output volume must not prevent stopping our live trace.
    try:
        save(directory, state)
    except OSError as error:
        print(f"WARNING: could not update private capture metadata: {error}", file=sys.stderr, flush=True)


def finish(directory, backend, state=None):
    """Stop only this helper's recorded, uniquely named instance; never cancel."""
    if not backend.is_admin():
        raise ValueError("Run the capture helper as administrator, NOT the game")
    directory = Path(directory).resolve()
    recovery = state is None
    if recovery:
        state = json.loads((directory / "capture-state.json").read_text(encoding="utf-8"))
    if not isinstance(state, dict) or not isinstance(state.get("instance"), str):
        raise ValueError("Invalid capture ownership metadata; no session touched")
    phases = ("recording", "stop-pending", "stop-failed")
    if recovery:
        # Explicit user recovery also handles a console/OS termination between
        # start and confirmation. Still target ONLY the random name we requested.
        phases += ("start-pending", "start-failed")
    if (state.get("tool") != TOOL_ID or state.get("directory") != str(directory) or
            not re.fullmatch(r"DeadSpaceCpu-[0-9a-f]{32}", state.get("instance", "")) or
            state.get("phase") not in phases):
        raise ValueError("Not a recoverable recording owned by this helper; no session touched")
    trace = directory / "cpu.etl"
    if trace.exists() or trace.is_symlink():
        raise ValueError("cpu.etl already exists; refusing to overwrite it or stop another session")
    state["phase"] = "stop-pending"
    state["stop_requested_utc"] = utc()
    remember(directory, state)
    try:
        result = backend.run(["-stop", str(trace), "Private user-led gameplay CPU sample",
                              "-skipPdbGen", "-compress", "-instancename", state["instance"]])
        # Each recovery attempt gets a fresh diagnostic file; never truncate one.
        try:
            record_command(directory, "wpr-stop-" + uuid.uuid4().hex + ".txt", result)
        except OSError as error:
            print(f"WARNING: could not save WPR stop output: {error}", file=sys.stderr, flush=True)
        if result.returncode:
            raise ValueError(f"WPR stop failed (exit {result.returncode}); keep this folder for recovery")
        state["phase"] = "stopped"
        state["stopped_utc"] = utc()
        if not trace.is_file() or trace.stat().st_size == 0:
            state["phase"] = "stopped-without-trace"
            raise ValueError("WPR reported stopped but no nonempty ETL was produced; no successful capture claimed")
    except BaseException:
        if state["phase"] == "stop-pending":
            state["phase"] = "stop-failed"
        remember(directory, state)
        raise
    remember(directory, state)
    return trace


def capture(executable, directory, seconds, backend):
    if not 15 <= seconds <= 120:
        raise ValueError("Capture duration must be 15..120 seconds")
    if not backend.is_admin():
        raise ValueError("Run the capture helper as administrator, NOT the game; no recording started")
    executable = Path(executable).resolve()
    directory = Path(directory).resolve()
    if not executable.is_file() or not PROFILE.is_file():
        raise ValueError("Expected port EXE or bundled WPR profile is missing")
    if directory.exists():
        raise ValueError("Output must be a NEW directory; nothing was replaced")
    processes = backend.processes()
    if (len(processes) != 1 or not processes[0].get("exe") or
            Path(processes[0]["exe"]).resolve() != executable):
        raise ValueError("Start exactly ONE matching test EXE yourself first; helper never launches/stops games")
    status = backend.run(["-status"])
    # Conservative, English-WPR guard. Unknown/localized status refuses rather
    # than assuming it is safe to interfere. Never cancel somebody's recording.
    if status.returncode or "WPR is not recording" not in status.stdout:
        raise ValueError("WPR is active or status is unrecognized; no recording started, no session cancelled")
    directory.mkdir(parents=True)
    state = {"tool": TOOL_ID, "directory": str(directory), "instance": "DeadSpaceCpu-" + uuid.uuid4().hex,
             "phase": "start-pending", "seconds_requested": seconds, "requested_utc": utc(),
             "process": processes[0], "exe_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
             "profile_sha256": hashlib.sha256(PROFILE.read_bytes()).hexdigest(),
             "privacy": "PRIVATE SYSTEM-WIDE CPU/STACK/PROCESS/MODULE METADATA; no game memory dump",
             "game_control": "none; user launched and controls game; helper never terminates it"}
    save(directory, state)
    result = backend.run(["-start", str(PROFILE.resolve()) + "!DeadSpaceCpu", "-instancename", state["instance"]])
    # Enter the stop-own-instance guard immediately after confirmed start, even
    # if writing diagnostics/metadata fails. Failed starts NEVER trigger a stop.
    if result.returncode:
        state["phase"] = "start-failed"
        save(directory, state)
        record_command(directory, "wpr-start.txt", result)
        raise ValueError(f"WPR start failed (exit {result.returncode}); no other session stopped/cancelled")
    state["phase"] = "recording"
    state["started_utc"] = utc()
    try:
        save(directory, state)
        record_command(directory, "wpr-start.txt", result)
        print(f"Recording {seconds} seconds. Return to your already running game and play normally.", flush=True)
        print("First 10 seconds allow returning to the game. Do not change renderer/settings.", flush=True)
        backend.sleep(seconds)
    finally:
        # In-memory ownership survives a metadata-write failure after start.
        # Never make stopping our live trace depend on writing another file.
        state["phase"] = "recording"
        trace = finish(directory, backend, state)
    return trace


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    start = actions.add_parser("capture", help="User-run, bounded capture of an already running matching game")
    start.add_argument("--exe", type=Path, required=True)
    start.add_argument("--output", type=Path, required=True)
    start.add_argument("--seconds", type=int, default=60)
    stop = actions.add_parser("finish", help="Recover ONLY this helper's own interrupted recording")
    stop.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        backend = WindowsBackend()
        trace = (capture(args.exe, args.output, args.seconds, backend) if args.action == "capture"
                 else finish(args.directory, backend))
        print(f"Stopped own recording. Keep trace PRIVATE: {trace}")
        print("The game was not stopped. Quit it normally yourself afterward; keep its complete deadspace.log.")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"CPU capture failed/refused: {error}\nKeep any capture-state.json for recovery; never use wpr -cancel.\n")
    except KeyboardInterrupt:
        parser.exit(130, "Interrupted; normal cleanup attempted for our named session only. Keep capture-state.json.\n")


if __name__ == "__main__":
    main()
