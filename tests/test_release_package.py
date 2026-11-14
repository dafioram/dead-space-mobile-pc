"""Only synthetic port files/notices are used; no game assets or executable code."""
import hashlib
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import release_package as release


class ReleaseTests(unittest.TestCase):
    tag = "v1.1-rc1"
    commit = "1" * 40

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "staging"
        self.stage.mkdir()

    def populate(self, platform="windows-x86_64"):
        binary = "deadspace.exe" if platform == "windows-x86_64" else "deadspace"
        for name in (binary, "README.md", "DEVELOPMENT.md", "licenses/synthetic.txt"):
            path = self.stage / name
            path.parent.mkdir(exist_ok=True)
            path.write_text("synthetic port/notices", encoding="utf-8")
        (self.stage / binary).chmod(0o755)
        (self.stage / "BUILD_INFO.txt").write_text(
            f"Platform: {platform}\nCommit: {self.commit}\nTag: {self.tag}\n", encoding="utf-8")
        self.checksums()

    def checksums(self):
        lines = []
        for path in sorted(self.stage.rglob("*")):
            if path.is_file() and path.name != "SHA256SUMS.txt":
                lines.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(self.stage).as_posix()}")
        (self.stage / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def test_version_tags_are_safe_and_rc_tag_is_supported(self):
        for tag in ("v1.1-rc1", "v1.1.0", "v2.0.1-rc.2"):
            self.assertEqual(release.validate_tag(tag), tag)
        for tag in ("../main", "main", "v1.1;echo", "v1.1/rc1", "v1.1-../rc1", "v1.1\n", "v1.1-rc..1"):
            with self.assertRaises(ValueError):
                release.validate_tag(tag)

    def test_round_trip_windows(self):
        self.populate()
        archive = release.package(self.stage, self.root / "out", "windows-x86_64", self.commit, self.tag)
        release.verify_archive(archive, "windows-x86_64", self.commit, self.tag)
        with zipfile.ZipFile(archive) as z:
            self.assertTrue(all(n.startswith(archive.stem + "/") for n in z.namelist()))

    def test_linux_zip_retains_executable_permissions(self):
        self.populate("linux-x86_64")
        archive = release.package(self.stage, self.root / "out", "linux-x86_64", self.commit, self.tag)
        with zipfile.ZipFile(archive) as z:
            mode = z.getinfo(archive.stem + "/deadspace").external_attr >> 16
            self.assertTrue(stat.S_ISREG(mode))
            if sys.platform != "win32":
                self.assertTrue(mode & stat.S_IXUSR)

    def test_wrong_provenance_and_modified_binary_rejected(self):
        self.populate()
        for commit, tag in (("2" * 40, self.tag), (self.commit, "v1.2")):
            with self.assertRaises(ValueError):
                release.verify_stage(self.stage, "windows-x86_64", commit, tag)
        (self.stage / "deadspace.exe").write_text("changed", encoding="utf-8")
        with self.assertRaises(ValueError):
            release.verify_stage(self.stage, "windows-x86_64", self.commit, self.tag)

    def test_unknown_and_private_files_rejected_even_with_checksums(self):
        self.populate()
        for name in ("notes.txt", "private.apk"):
            extra = self.stage / name
            extra.write_text("synthetic", encoding="utf-8")
            self.checksums()
            with self.assertRaises(ValueError):
                release.verify_stage(self.stage, "windows-x86_64", self.commit, self.tag)
            extra.unlink()

    def test_existing_output_never_replaced(self):
        self.populate()
        out = self.root / "out"
        out.mkdir()
        (out / "keep.txt").write_text("keep", encoding="utf-8")
        with self.assertRaises(ValueError):
            release.package(self.stage, out, "windows-x86_64", self.commit, self.tag)
        self.assertEqual((out / "keep.txt").read_text(), "keep")

    def test_archive_corruption_rejected(self):
        self.populate()
        archive = release.package(self.stage, self.root / "out", "windows-x86_64", self.commit, self.tag)
        with archive.open("ab") as stream:
            stream.write(b"synthetic trailing change")
        with self.assertRaises(ValueError):
            release.verify_archive(archive, "windows-x86_64", self.commit, self.tag)

    def test_tag_resolution_checks_main_ancestry(self):
        with patch("subprocess.check_output", return_value=self.commit + "\n") as output, patch("subprocess.run") as run:
            self.assertEqual(release.release_commit(self.root, self.tag), self.commit)
            self.assertEqual(output.call_args.args[0][-4:], ["rev-list", "--max-count=1", "refs/tags/" + self.tag, "--"])
            self.assertEqual(run.call_args.args[0][-4:], ["merge-base", "--is-ancestor", self.commit, "origin/main"])


@unittest.skipUnless(shutil.which("git"), "real-Git release tests require Git")
class ReleaseGitTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="deadspace release git-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        # Never use the developer's identity, signing setup or hooks in fixtures.
        env = {"GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1", "LC_ALL": "C",
               "GIT_AUTHOR_NAME": "Synthetic release test", "GIT_AUTHOR_EMAIL": "test@example.invalid",
               "GIT_COMMITTER_NAME": "Synthetic release test", "GIT_COMMITTER_EMAIL": "test@example.invalid"}
        environment = patch.dict(os.environ, env)
        environment.start()
        self.addCleanup(environment.stop)
        self.git("init", "-q", "-b", "main")
        (self.root / "synthetic.txt").write_text("synthetic release source\n", encoding="utf-8")
        self.git("add", "synthetic.txt")
        self.git("commit", "-qm", "synthetic base")
        self.commit = self.git("rev-parse", "HEAD")
        self.git("update-ref", "refs/remotes/origin/main", self.commit)
        self.git("tag", "v1.1-rc2")

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], text=True).strip()

    def test_lightweight_and_annotated_tags(self):
        self.assertEqual(release.release_commit(self.root, "v1.1-rc2"), self.commit)
        self.git("tag", "-a", "v1.1-rc3", "-m", "synthetic annotated tag")
        self.assertEqual(release.release_commit(self.root, "v1.1-rc3"), self.commit)

    def test_missing_tag_and_non_main_commit_rejected(self):
        with self.assertRaises(subprocess.CalledProcessError):
            release.release_commit(self.root, "v9.9")
        self.git("checkout", "-qb", "playtest")
        (self.root / "synthetic.txt").write_text("synthetic integration-only change\n", encoding="utf-8")
        self.git("commit", "-qam", "synthetic integration")
        self.git("tag", "v1.1-rc4")
        with self.assertRaises(subprocess.CalledProcessError):
            release.release_commit(self.root, "v1.1-rc4")

    def test_non_commit_tag_rejected(self):
        blob = self.git("rev-parse", "HEAD:synthetic.txt")
        self.git("tag", "v1.1-blob", blob)
        with self.assertRaises((ValueError, subprocess.CalledProcessError)):
            release.release_commit(self.root, "v1.1-blob")

    @unittest.skipUnless(os.name == "nt", "Windows command-wrapper regression")
    def test_windows_command_wrapper_does_not_strip_tag_resolution(self):
        # Reproduce the old failure even though this tag demonstrably exists.
        old = subprocess.run(["cmd.exe", "/d", "/c", "git", "-C", str(self.root), "rev-parse", "--verify",
                              "refs/tags/v1.1-rc2^{commit}"], capture_output=True, text=True)
        self.assertNotEqual(old.returncode, 0)
        check_output = subprocess.check_output
        def wrapped(command, **kwargs):
            return check_output(["cmd.exe", "/d", "/c", *command], **kwargs)
        with patch.object(release.subprocess, "check_output", side_effect=wrapped):
            self.assertEqual(release.release_commit(self.root, "v1.1-rc2"), self.commit)


if __name__ == "__main__":
    unittest.main()
