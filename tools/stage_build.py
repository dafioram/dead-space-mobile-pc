#!/usr/bin/env python3
"""Create a fresh, allowlisted CI artifact directory. Never copy a build tree."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

from check_assets import check_package
from release_package import release_commit


ROOT = Path(__file__).resolve().parents[1]
BUNDLED_NOTICES = {
    "LICENSE.txt": "dynarmic.txt",
    "externals/fmt/LICENSE": "fmt.txt",
    "externals/mcl/LICENSE": "mcl.txt",
    "externals/robin-map/LICENSE": "robin-map.txt",
    "externals/xbyak/COPYRIGHT": "xbyak.txt",
    "externals/zycore/LICENSE": "zycore.txt",
    "externals/zydis/LICENSE": "zydis.txt",
}
MINGW_NOTICES = ("SDL2", "zlib", "libgcc", "libstdc++", "libwinpthread", "headers", "crt", "libiconv")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=("windows-x86_64", "linux-x86_64"))
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mingw-prefix", type=Path, help="Windows toolchain prefix, e.g. /mingw64")
    parser.add_argument("--source-root", type=Path, help="source checkout supplying docs, notices and provenance")
    parser.add_argument("--release-tag", help="require HEAD to match this main-ancestry tag")
    args = parser.parse_args()
    root = args.source_root.resolve() if args.source_root else ROOT
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if args.release_tag:
        try:
            if release_commit(root, args.release_tag) != revision:
                parser.error("release staging requires HEAD to be the tagged commit")
        except (ValueError, subprocess.CalledProcessError) as error:
            parser.error(f"cannot validate release tag before staging: {error}")
    executable = "deadspace.exe" if args.platform == "windows-x86_64" else "deadspace"
    files = {
        args.build_dir / executable: executable,
        root / "README.md": "README.md",
        root / "docs" / "DEVELOPMENT.md": "DEVELOPMENT.md",
        root / "docs" / "licenses" / "Boost-1.0.txt": "licenses/Boost-1.0.txt",
    }
    for source, destination in BUNDLED_NOTICES.items():
        files[root / "external" / "dynarmic" / source] = "licenses/" + destination
    if args.platform == "windows-x86_64":
        if not args.mingw_prefix:
            parser.error("Windows staging requires --mingw-prefix for runtime/dependency notices")
        for dependency in MINGW_NOTICES:
            directory = args.mingw_prefix / "share" / "licenses" / dependency
            notices = sorted(p for p in directory.rglob("*") if p.is_file())
            if not notices:
                parser.error(f"missing dependency notices: {directory}")
            for source in notices:
                files[source] = f"licenses/{dependency}/{source.relative_to(directory).as_posix()}"
    # Refuse reuse rather than risking stale private data or deleting user files.
    if args.output.exists():
        parser.error("output must not already exist; choose a fresh staging directory")
    for source in files:
        if not source.is_file() or source.is_symlink():
            parser.error(f"missing or unsafe allowlisted input: {source}")
    args.output.mkdir(parents=True)
    for source, destination in files.items():
        (args.output / destination).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, args.output / destination)
    build_kind = (f"Tag: {args.release_tag}\nRelease build" if args.release_tag else "Development build")
    (args.output / "BUILD_INFO.txt").write_text(
        f"Platform: {args.platform}\nCommit: {revision}\n"
        f"{build_kind}: no game data included; supply your own APK.\n"
        "Linux requires SDL2, OpenGL, zlib and the host C/C++ runtimes.\n",
        encoding="utf-8",
    )
    checksums = []
    for path in sorted(args.output.rglob("*")):
        if path.is_file():
            checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(args.output).as_posix()}")
    with (args.output / "SHA256SUMS.txt").open("w", encoding="utf-8", newline="\n") as stream:
        stream.write("\n".join(checksums) + "\n")
    failures = check_package(args.output)
    if failures:
        parser.exit(1, "Staging rejected:\n" + "\n".join(failures) + "\n")
    print(f"Staged checked {args.platform} development build in {args.output}")


if __name__ == "__main__":
    main()
