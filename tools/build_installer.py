#!/usr/bin/env python3
"""One-time Windows installer build from a checked, exact-tag portable staging area."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from check_assets import check_package
from release_package import release_commit, validate_tag, verify_stage

ROOT = Path(__file__).resolve().parents[1]


def resource_bytes(executable, name):
    # Read resources as data: never run the installer or touch the user's install.
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
    api.LoadLibraryExW.restype = ctypes.c_void_p
    api.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_void_p]
    api.FindResourceW.restype = ctypes.c_void_p
    api.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    api.LoadResource.restype = ctypes.c_void_p
    api.LockResource.argtypes = [ctypes.c_void_p]
    api.LockResource.restype = ctypes.c_void_p
    api.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    api.SizeofResource.restype = ctypes.c_uint32
    api.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = api.LoadLibraryExW(str(executable.resolve()), None, 0x22)
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        resource = api.FindResourceW(module, name, ctypes.c_void_p(10))
        loaded = api.LoadResource(module, resource) if resource else None
        address = api.LockResource(loaded) if loaded else None
        size = api.SizeofResource(module, resource) if resource else 0
        if not address or not size:
            raise ValueError(f"missing installer resource: {name}")
        return ctypes.string_at(address, size)
    finally:
        api.FreeLibrary(module)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--staging", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new output directory")
    parser.add_argument("--mingw-prefix", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, help="parent of the disposable compiler directory")
    args = parser.parse_args()
    try:
        if os.name != "nt":
            raise ValueError("installer compilation/resource verification requires Windows")
        validate_tag(args.tag)
        commit = release_commit(args.source_root, args.tag)
        head = subprocess.check_output(["git", "-C", str(args.source_root), "rev-parse", "HEAD"], text=True).strip()
        if head != commit:
            raise ValueError("application source must be checked out at the exact tag")
        stage = args.staging.resolve()
        paths = verify_stage(stage, "windows-x86_64", commit, args.tag)
        if args.output.exists():
            raise ValueError("installer output must be a new directory")
        version = args.tag[1:]
        numbers = version.split("-", 1)[0].split(".")
        numeric = [int(n) for n in numbers] + [0] * (4 - len(numbers))
        if any(n > 65535 for n in numeric):
            raise ValueError("installer numeric version components must fit 16 bits")
        notices = "Third-party notices for Dead Space Mobile PC Port\n\n"
        for name, path in sorted(paths.items()):
            if name.startswith("licenses/"):
                notices += f"\n===== {name} =====\n" + path.read_text(encoding="utf-8", errors="replace") + "\n"
        tools_revision = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip()
        info = paths["BUILD_INFO.txt"].read_text(encoding="utf-8") + f"Installer packaging commit: {tools_revision}\n"
        args.output.mkdir(parents=True)
        output = args.output.resolve() / f"DeadSpaceMobilePCPort-{args.tag}-Setup.exe"
        with tempfile.TemporaryDirectory(prefix="deadspace-installer-", dir=args.work_dir) as temporary:
            work = Path(temporary)
            (work / "payload").mkdir()
            shutil.copy2(paths["deadspace.exe"], work / "payload/deadspace.exe")
            (work / "payload/THIRD_PARTY_NOTICES.txt").write_text(notices, encoding="utf-8")
            (work / "payload/BUILD_INFO.txt").write_text(info, encoding="utf-8")
            definitions = (f'#define APP_VERSION L"{version}"\n#define APP_VERSION_TEXT "{version}"\n'
                           f'#define APP_VERSION_NUMERIC {",".join(map(str, numeric))}\n')
            (work / "version.h").write_text(definitions, encoding="utf-8")
            (work / "installer.rc").write_text(definitions + (ROOT / "installer/installer.rc").read_text(encoding="utf-8"), encoding="utf-8")
            shutil.copy2(ROOT / "installer/installer.manifest", work / "installer.manifest")
            compiler = args.mingw_prefix.resolve() / "bin/g++.exe"
            windres = args.mingw_prefix.resolve() / "bin/windres.exe"
            build_env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get("PATH", ""))
            subprocess.run([str(windres), "installer.rc", "-O", "coff", "-o", "installer.res"], cwd=work, env=build_env, check=True)
            subprocess.run([str(compiler), "-std=c++20", "-O2", "-municode", "-mwindows", "-static",
                            "-include", str(work / "version.h"), "-I" + str(args.source_root.resolve() / "src"),
                            str(ROOT / "installer/installer.cpp"), str(args.source_root.resolve() / "src/apk.cpp"),
                            str(work / "installer.res"), "-o", str(output), "-lz", "-lcomctl32", "-lcomdlg32",
                            "-lshell32", "-lole32", "-luuid", "-ladvapi32"], cwd=work, env=build_env, check=True)
            for name, expected in (("PAYLOAD_EXE", paths["deadspace.exe"].read_bytes()),
                                   ("PAYLOAD_NOTICES", (work / "payload/THIRD_PARTY_NOTICES.txt").read_bytes()),
                                   ("PAYLOAD_INFO", (work / "payload/BUILD_INFO.txt").read_bytes())):
                if resource_bytes(output, name) != expected:
                    raise ValueError(f"installer resource verification failed: {name}")
        failures = check_package(args.output)
        if failures:
            raise ValueError("; ".join(failures))
        digest = hashlib.sha256(output.read_bytes()).hexdigest()
        with output.with_suffix(".exe.sha256").open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(f"{digest}  {output.name}\n")
        print(f"Verified installer (tag payload byte-identical; installer not executed): {output}")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Installer build failed: {error}\n")


if __name__ == "__main__":
    main()
