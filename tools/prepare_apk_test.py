#!/usr/bin/env python3
"""Prepare ONE fresh private APK test case. Never starts gameplay.

The only executable operation is the port's explicit --import-only mode.
Launch.cmd/Launch.sh is generated for the USER to run later. No original profile
is read, copied or modified; this case starts with its own profile on first play.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess


def prepare(exe, apk, case):
    exe, apk, case = exe.resolve(), apk.resolve(), case.absolute()
    if not exe.is_file() or not apk.is_file():
        raise ValueError("EXE and APK must be existing files")
    # Refuse reuse; don't delete somebody's progress or replace a test executable.
    case.parent.mkdir(parents=True, exist_ok=True)
    case.mkdir()
    target = case / ("deadspace.exe" if exe.suffix.lower() == ".exe" else "deadspace")
    shutil.copy2(exe, target)
    if os.name != "nt":
        target.chmod(target.stat().st_mode | 0o100)
    with (case / "import.log").open("xb") as log:
        result = subprocess.run([str(target), "--apk", str(apk), "--game-dir", str(case / "gamedata"),
                                 "--import-only"], cwd=case, stdout=log, stderr=subprocess.STDOUT, timeout=300)
    if result.returncode:
        raise ValueError(f"import refused/failed (exit {result.returncode}); no launcher created; see {case / 'import.log'}")
    sha = hashlib.sha256()
    with target.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    digest = sha.hexdigest()
    (case / "TEST_CASE.txt").write_text(
        f"PRIVATE GAME DATA AND FUTURE SAVES: do not commit/distribute this folder.\n"
        f"APK: {apk.name}\nEXE SHA256: {digest}\n"
        "Prepared via --import-only. No game session was started.\n"
        "Launch.cmd / Launch.sh is for the user only. Each case uses its own userdata and logs.\n"
        "For progression testing omit --no-save; for a disposable run append --no-save.\n"
        "Original installations/profiles are not copied or overwritten.\n", encoding="utf-8")
    if target.suffix.lower() == ".exe":
        (case / "Launch.cmd").write_bytes(
            b'@echo off\r\ncd /d "%~dp0"\r\n"%~dp0deadspace.exe" --game-dir "%~dp0gamedata" '
            b'--data "%~dp0userdata" --log "%~dp0deadspace.log" %*\r\nexit /b %errorlevel%\r\n')
    else:
        launch = case / "Launch.sh"
        launch.write_text('#!/bin/sh\ncd -- "$(dirname -- "$0")" || exit 1\nexec ./deadspace '
                          '--game-dir "$PWD/gamedata" --data "$PWD/userdata" --log "$PWD/deadspace.log" "$@"\n', encoding="utf-8")
        launch.chmod(0o700)
    print(f"Prepared (NOT played): {case}")
    return case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--apk", type=Path, required=True)
    parser.add_argument("--case-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.exe, args.apk, args.case_dir)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Preparation failed: {error}\n")


if __name__ == "__main__":
    main()
