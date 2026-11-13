# Development and private game data

## Never commit or distribute game data

Supply your own legally obtained APK. Keep APKs, OBBs, extracted assets and ARM
libraries outside this checkout when possible. Imported `gamedata/`, `assets/`,
saves and dumped textures are ignored as a second line of defence. Do not use
`git add -f` to bypass these rules. Do not upload private data as CI artifacts,
test fixtures, release attachments or diagnostic bundles.

Before every commit, check the **indexed** files:

```sh
python tools/check_assets.py --staged
```

Audit the whole index (including files that were force-added or already committed):

```sh
python tools/check_assets.py --tracked
python -m unittest discover -s tests -p "test_asset_policy.py" -v
```

Before publishing, copy only explicitly selected port binaries and documentation
to a clean staging directory, then check it:

```sh
python tools/check_assets.py --package release/staging
```

The checker rejects known private paths/extensions, Android DEX and ARM32 ELF
content, and private files inside ZIP archives, including nested archives. It is
a safety net, not a copyright classifier: renamed textures and arbitrary opaque
archives cannot all be identified automatically. Never package a whole working
tree or build directory. Submodule contents are not scanned by the index check;
third-party dependencies must remain pinned and reviewed separately.

Tests must generate tiny synthetic data in temporary directories. Never copy
fixtures from the APK, even when testing a reported game-data bug. Gameplay
checks requiring real assets or saves stay local and are reported separately
from public, asset-free tests.

## Asset-free tests

The test-only build needs a C++20 compiler, CMake, Python 3.8+ and zlib development
files. It does not need SDL2, OpenGL, Dynarmic, its submodules, or an APK:

```sh
cmake -S . -B build-tests -DDEADSPACE_BUILD_APP=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

On Windows, use an MSYS2 MinGW/UCRT shell with its compiler and zlib installed.
The normal application build includes these tests by default; pass
`-DBUILD_TESTING=OFF` to opt out. Tests keep checks enabled in release builds and
generate all APK/ELF-like inputs in temporary directories. The generated ELF
header is deliberately non-executable and contains no game code.

Coverage includes the production APK importer (supported layouts and ZIP
compression, texture-mode markers, missing/invalid/truncated inputs), settings
parsing/persistence, and the same file-opening helper used by JNI AssetManager.
Directory rejection is the regression check for upstream issue #5/PR #9. Python
tests also check the asset policy and gameplay smoke-test exit status.

This is a baseline regression suite, not a claim that the importer handles every
malformed archive safely. CRC verification and comprehensive importer hardening
remain separate work. Tests do not launch the JIT, render, or validate gameplay.

## Commit attribution

Commits made for this fork should name David as author with
`5838258+dafioram@users.noreply.github.com`. AI-assisted commits may include a
`Co-authored-by` trailer; do not replace the user's authorship.
