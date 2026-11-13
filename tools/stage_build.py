#!/usr/bin/env python3
"""Create a fresh, allowlisted CI artifact directory. Never copy a build tree."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

from check_assets import check_package


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
    args = parser.parse_args()
    executable = "deadspace.exe" if args.platform == "windows-x86_64" else "deadspace"
    files = {
        args.build_dir / executable: executable,
        ROOT / "README.md": "README.md",
        ROOT / "docs" / "DEVELOPMENT.md": "DEVELOPMENT.md",
        ROOT / "docs" / "licenses" / "Boost-1.0.txt": "licenses/Boost-1.0.txt",
    }
    for source, destination in BUNDLED_NOTICES.items():
        files[ROOT / "external" / "dynarmic" / source] = "licenses/" + destination
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
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    (args.output / "BUILD_INFO.txt").write_text(
        f"Platform: {args.platform}\nCommit: {revision}\n"
        "Development build: no game data included; supply your own APK.\n"
        "Linux requires SDL2, OpenGL, zlib and the host C/C++ runtimes.\n",
        encoding="utf-8",
    )
    checksums = []
    for path in sorted(args.output.rglob("*")):
        if path.is_file():
            checksums.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(args.output).as_posix()}")
    (args.output / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
    failures = check_package(args.output)
    if failures:
        parser.exit(1, "Staging rejected:\n" + "\n".join(failures) + "\n")
    print(f"Staged checked {args.platform} development build in {args.output}")


if __name__ == "__main__":
    main()
