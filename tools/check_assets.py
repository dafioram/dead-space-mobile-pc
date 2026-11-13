#!/usr/bin/env python3
"""Reject private game data in the Git index or a release staging directory."""
import argparse
import io
from pathlib import Path, PurePosixPath
import struct
import subprocess
import sys
import zipfile


PRIVATE_DIRS = {"apk", "assets", "gamedata", "userdata"}
PRIVATE_SUFFIXES = {".apk", ".obb", ".so", ".sbin", ".m3g", ".pvr"}


def path_problem(name):
    path = PurePosixPath(name.replace("\\", "/").lower())
    parts = path.parts
    if path.is_absolute() or ".." in parts or any(":" in p for p in parts):
        return "unsafe path"
    if PRIVATE_DIRS.intersection(parts[:-1]):
        return "private game-data directory"
    if any(parts[i:i + 3] == ("mods", "textures", "_dump") for i in range(len(parts) - 2)):
        return "dumped game textures"
    if path.suffix in PRIVATE_SUFFIXES or path.name == "libdeadspace.so":
        return "game archive, library, or asset extension"
    if path.name in {"androidmanifest.xml", "classes.dex"}:
        return "Android package contents"
    return None


def content_problem(stream, depth=0):
    head = stream.read(20)
    stream.seek(0)
    if head.startswith(b"dex\n"):
        return "Android DEX content"
    if len(head) >= 20 and head[:5] == b"\x7fELF\x01" and head[5] in (1, 2):
        endian = "<" if head[5] == 1 else ">"
        if struct.unpack(endian + "H", head[18:20])[0] == 40:
            return "32-bit ARM ELF content"
    if not zipfile.is_zipfile(stream):
        return None
    if depth >= 4:
        return "archive nesting exceeds inspection limit"
    stream.seek(0)
    try:
        with zipfile.ZipFile(stream) as archive:
            entries = archive.infolist()
            if len(entries) > 10000 or sum(e.file_size for e in entries) > 256 * 1024 * 1024:
                return "archive exceeds inspection limits"
            for entry in entries:
                problem = path_problem(entry.filename)
                if problem:
                    return f"archive member {entry.filename}: {problem}"
                if entry.is_dir():
                    continue
                with archive.open(entry) as member:
                    problem = content_problem(member, depth + 1)
                if problem:
                    return f"archive member {entry.filename}: {problem}"
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        return f"cannot inspect archive: {error}"
    return None


def git(*args):
    return subprocess.check_output(["git", *args])


def check_index(staged):
    selected = None
    if staged:
        selected = set(git("diff", "--cached", "--name-only", "--diff-filter=ACMRT", "-z").split(b"\0"))
    failures = []
    for record in git("ls-files", "--stage", "-z").split(b"\0"):
        if not record:
            continue
        metadata, raw_name = record.split(b"\t", 1)
        if selected is not None and raw_name not in selected:
            continue
        mode, oid, stage = metadata.split()
        name = raw_name.decode("utf-8", errors="surrogateescape")
        problem = path_problem(name)
        if not problem and stage != b"0":
            problem = "unresolved index entry"
        if not problem and mode == b"160000":
            continue  # Pinned third-party submodule, not its working tree.
        if not problem:
            # Inspect the indexed blob, never an unstaged replacement in the working tree.
            problem = content_problem(io.BytesIO(git("cat-file", "blob", oid.decode("ascii"))))
        if problem:
            failures.append(f"{name}: {problem}")
    return failures


def check_package(directory):
    failures = []
    root = Path(directory)
    if not root.is_dir() or root.is_symlink():
        return [f"{directory}: expected a real release staging directory"]
    for path in sorted(root.rglob("*")):
        name = path.relative_to(root).as_posix()
        problem = path_problem(name)
        if path.is_symlink():
            problem = "symlinks are not allowed in release staging"
        elif path.is_file() and not problem:
            with path.open("rb") as stream:
                problem = content_problem(stream)
        if problem:
            failures.append(f"{name}: {problem}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--staged", action="store_true", help="check staged additions/changes (default)")
    source.add_argument("--tracked", action="store_true", help="check every indexed file")
    source.add_argument("--package", metavar="DIRECTORY", help="check a release staging directory")
    args = parser.parse_args()
    try:
        failures = check_package(args.package) if args.package else check_index(not args.tracked)
    except (OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f"Asset policy check failed: {error}", file=sys.stderr)
        return 1
    if failures:
        print("Asset policy rejected the following files:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("Asset policy check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
