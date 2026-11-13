# Releases: portable assets and a manual Windows installer

## Automatic portable assets

After `.github/workflows/release.yml` is merged into `main`, publishing a release
runs Windows/Linux builds, tests and allowlisted staging, then attaches:

- `deadspace-TAG-windows-x86_64-portable.zip` and `.zip.sha256`
- `deadspace-TAG-linux-x86_64-portable.zip` and `.zip.sha256`

Both archives contain the program, documentation, dependency notices, exact tag/
commit information and per-file checksums. They contain no APKs, game libraries,
assets, saves, recordings or private fixtures. Windows is a standalone executable;
Linux still requires the host SDL2/OpenGL/zlib/runtime libraries documented in the
build instructions. Its ZIP preserves the executable file mode; it is not an AppImage.

Create the tag from the intended merged `main` commit. Never release a playtest-only
integration commit or move an existing release tag to pick up later fixes. The
workflow requires main ancestry and an existing published release, pins the source
commit, and rechecks that the tag did not move before uploading. Build jobs have
read-only permissions; only the final verified-asset upload job can write releases.
It never overwrites existing assets or changes release notes/prerelease status.

For a release published before this workflow existed, run **Portable release
assets → Run workflow** from `main`, entering its existing tag (e.g. `v1.1-rc1`).
This rebuilds that tag, not current main. Packaging tools are checked out separately
so older tags can be backfilled. If a prior upload partially succeeded, review the
existing assets rather than automatically deleting/replacing them.

Ordinary CI artifacts are still temporary (14 days). Assets attached to a GitHub
release do not use that CI retention period. No installer is built automatically.

## One-time Windows installer

Use the current packaging tools, an exact-tag application checkout and a Windows
MinGW toolchain. First stage the tagged Windows build with `tools/stage_build.py`
using `--source-root SOURCE --release-tag TAG`; then invoke:

```text
python tools/build_installer.py --tag TAG --source-root SOURCE --staging STAGING --output NEW_OUTPUT --mingw-prefix MINGW_PREFIX
```

The builder embeds the byte-identical staged game executable, combined third-party
notices and build information, with versioned setup metadata. It verifies those
resources without executing the installer. The packaging-tool revision is recorded
separately from the game payload revision. `tools/mkinstaller.sh TAG [BUILD_DIR]`
is a convenience wrapper when the source itself is checked out at that tag.

Manually attach `DeadSpaceMobilePCPort-TAG-Setup.exe` and its `.exe.sha256` to the
same release. It installs per-user under `%LOCALAPPDATA%\Programs\Dead Space Mobile PC Port`,
creates shortcuts and asks the user for their APK (or preserves existing imported
data when chosen). It also installs `THIRD_PARTY_NOTICES.txt` and `BUILD_INFO.txt`.
Do not run installation/uninstallation tests against a real user installation.
An installer build/resource check is not a claim of an end-to-end installation test.
