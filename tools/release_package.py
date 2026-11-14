#!/usr/bin/env python3
"""Package/verify allowlisted portable releases, including backfills of old tags."""
import argparse
import hashlib
from pathlib import Path
import re
import stat
import subprocess
import tempfile
import zipfile

from check_assets import check_package, path_problem


PLATFORMS = ("windows-x86_64", "linux-x86_64")


def validate_tag(tag):
    if len(tag) > 100 or not re.fullmatch(r"v[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?", tag):
        raise ValueError("expected a version tag such as v1.1-rc1 or v1.1.0")
    return tag


def release_commit(root, tag):
    validate_tag(tag)
    def git(*args):
        return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()
    # A Windows command wrapper can strip the caret from ^{commit}. rev-list
    # peels lightweight/annotated tags without shell-sensitive revision syntax,
    # and still fails when the ref is missing or does not name a commit.
    commit = git("rev-list", "--max-count=1", f"refs/tags/{tag}", "--")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("invalid release commit")
    # Never turn a playtest-only integration commit into a production release.
    subprocess.run(["git", "-C", str(root), "merge-base", "--is-ancestor", commit, "origin/main"], check=True)
    return commit


def verify_stage(stage, platform, commit, tag):
    validate_tag(tag)
    if platform not in PLATFORMS or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("invalid platform or commit")
    failures = check_package(stage)
    if failures:
        raise ValueError("; ".join(failures))
    paths = {p.relative_to(stage).as_posix(): p for p in stage.rglob("*") if p.is_file()}
    binary = "deadspace.exe" if platform == "windows-x86_64" else "deadspace"
    required = {binary, "README.md", "DEVELOPMENT.md", "BUILD_INFO.txt", "SHA256SUMS.txt"}
    if not required <= paths.keys() or not any(n.startswith("licenses/") for n in paths):
        raise ValueError("incomplete release staging")
    if any(n not in required and not n.startswith("licenses/") for n in paths):
        raise ValueError("unexpected file outside the release allowlist")
    info = paths["BUILD_INFO.txt"].read_text(encoding="utf-8").splitlines()
    if any(line not in info for line in (f"Platform: {platform}", f"Commit: {commit}", f"Tag: {tag}")):
        raise ValueError("staging provenance does not match the requested release")
    expected = {}
    for line in paths["SHA256SUMS.txt"].read_text(encoding="utf-8").splitlines():
        digest, name = line.split("  ", 1)
        if not re.fullmatch(r"[0-9a-f]{64}", digest) or path_problem(name) or name in expected:
            raise ValueError("invalid checksum manifest")
        expected[name] = digest
    if expected.keys() != paths.keys() - {"SHA256SUMS.txt"}:
        raise ValueError("checksums must cover every staged file exactly once")
    for name, digest in expected.items():
        if hashlib.sha256(paths[name].read_bytes()).hexdigest() != digest:
            raise ValueError(f"checksum mismatch: {name}")
    return paths


def package(stage, output, platform, commit, tag):
    paths = verify_stage(stage, platform, commit, tag)
    if output.exists():
        raise ValueError("output must be a new directory")
    output.mkdir(parents=True)
    stem = f"deadspace-{tag}-{platform}-portable"
    archive = output / f"{stem}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name, path in sorted(paths.items()):
            z.write(path, f"{stem}/{name}")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    with archive.with_suffix(archive.suffix + ".sha256").open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{digest}  {archive.name}\n")
    verify_archive(archive, platform, commit, tag)
    return archive


def verify_archive(archive, platform, commit, tag):
    validate_tag(tag)
    stem = f"deadspace-{tag}-{platform}-portable"
    if archive.name != f"{stem}.zip":
        raise ValueError("unexpected archive name")
    expected = hashlib.sha256(archive.read_bytes()).hexdigest() + f"  {archive.name}\n"
    if archive.with_suffix(archive.suffix + ".sha256").read_text(encoding="utf-8") != expected:
        raise ValueError("archive checksum mismatch")
    failures = check_package(archive.parent)
    if failures:
        raise ValueError("; ".join(failures))
    with tempfile.TemporaryDirectory(prefix="deadspace-release-verify-") as temp, zipfile.ZipFile(archive) as z:
        names = set()
        for entry in z.infolist():
            if path_problem(entry.filename) or not entry.filename.startswith(stem + "/") or entry.filename in names:
                raise ValueError("unsafe/duplicate archive path")
            names.add(entry.filename)
            mode = entry.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValueError("archive symlinks are not allowed")
        if z.testzip() is not None:
            raise ValueError("archive CRC failure")
        z.extractall(temp)
        verify_stage(Path(temp) / stem, platform, commit, tag)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("resolve", "pack", "verify"))
    parser.add_argument("--tag", required=True)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--platform", choices=PLATFORMS)
    parser.add_argument("--commit")
    parser.add_argument("--staging", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--archive", type=Path)
    args = parser.parse_args()
    if args.action != "resolve" and (not args.platform or not args.commit):
        parser.error("pack/verify require --platform and --commit")
    if args.action == "pack" and (args.staging is None or args.output is None):
        parser.error("pack requires --staging and --output")
    if args.action == "verify" and args.archive is None:
        parser.error("verify requires --archive")
    try:
        if args.action == "resolve":
            print(release_commit(args.source_root, args.tag))
        elif args.action == "pack":
            print(package(args.staging, args.output, args.platform, args.commit or "", args.tag))
        else:
            verify_archive(args.archive, args.platform, args.commit or "", args.tag)
            print("Release archive verified")
    except (ValueError, OSError, subprocess.CalledProcessError, TypeError) as error:
        parser.exit(1, f"Release packaging failed: {error}\n")


if __name__ == "__main__":
    main()
