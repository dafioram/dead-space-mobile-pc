# Development and private game data

For console commands, debug keys, command-line help and disposable `--no-save`
sessions, see `debug_menu.md` alongside this file in the source documentation
(included as `docs/debug_menu.md` in portable packages). No-save sessions protect the
userdata source, but cannot protect it from a helper that deletes it before
launch, such as `tools/dev.py start --fresh`.

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

## Building from source

`external/dynarmic` is a clone of https://github.com/azahar-emu/dynarmic with its submodules
`fmt mcl robin-map xbyak zycore zydis` checked out (both platforms).

### Windows (MSYS2 MINGW64)

```
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,SDL2,boost,zlib}
git submodule update --init --recursive
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

The Windows build statically links SDL2 and the compiler runtimes, so those DLLs do not need to be
shipped separately. Windows system libraries and a working OpenGL driver are still required.

The installer (`installer/installer.cpp`, a small Win32 program with the checked `deadspace.exe` and
notices embedded as resources, and `src/apk.cpp` for the import) is built manually from an exact tagged
checkout; see [RELEASING.md](RELEASING.md).

### Linux (x86-64)

```
sudo apt install build-essential cmake ninja-build libsdl2-dev libboost-dev zlib1g-dev libgl-dev
git submodule update --init --recursive
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

The application build above needs SDL2, OpenGL and Dynarmic. For a lightweight test-only build that
needs none of them, see [Asset-free tests](#asset-free-tests).

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

Native tests also cover interaction-zone distance checks, including large objects
with distant origins, exact search boundaries, three-dimensional distance and
invalid bounds, plus floor/slope/wall/ceiling interaction frames and debug-volume
round trips. Their geometry is synthetic and is not copied from game assets.

Marker-based interaction zones retain their full world orientation while keeping
dimensions in metres (model scale is removed). Geometry checks cover rotated
corners and distances, reflected/invalid marker axes, strict box overlap, empty
corners of enclosing AABBs, and skew edge-edge separation. Selection and the debug
overlay use the same oriented boxes; separate markers are not joined by a union.

`scene_cache` compiles the production light/cache handlers with declaration-only
SDL/GL headers, synthetic memory and native-call stand-ins. It checks world/node
retirement and address reuse, construction readiness, live type/ownership gates,
normal fade timing, fixture/fog invalidation, temporary-reference balance, cyclic
parents, lock release around native transforms/releases, and read-only teardown
diagnostics. It executes no game instructions and requires no SDL/GL packages.

`save_io`, `save_adapters` and `save_processes` cover checked atomic profile I/O,
bounded SBIN inspection, actual descriptor/stdio handlers, native save/load return
policy with simulated registers, and helper-process lock/termination boundaries.
Fixtures/fuzz mutations are synthetic; no retail code, assets or saves are
executed or distributed. See `SAVE_LOAD_RELIABILITY.md` for guarantees and limits.
Helpers deliberately terminate only themselves, never a game process.

This is a baseline regression suite, not a claim that the importer handles every
malformed archive safely. CRC verification and comprehensive importer hardening
remain separate work. Tests do not launch the JIT, render, or validate gameplay.

## Ordinary host/input log context

Normal startup logging (no extra diagnostic flag) records the SDL-selected video
backend, compiled/runtime SDL versions and requested `SDL_VIDEODRIVER` override.
Linux additionally records kernel release/architecture, bounded desktop/session
environment hints and whether WSL environment markers are present. It does not
dump the environment, hostname, WSL distro name or display socket paths.

The selected SDL backend is the application's backend, not proof of the whole
desktop setup: `x11` can be XWayland, `wayland` can be nested Weston, and session/
WSL environment markers are hints only. Startup display/refresh, drawable size
and swap interval are SDL-reported configuration, not measured display cadence.

Mouse-mode transitions log the existing request's result/error and SDL-reported
relative state. A successful request does not establish host pointer confinement.
Logging does not change gameplay ownership, cursor policy, backend selection,
capture retries, graphics or frame pacing; failed requests keep existing behavior.
The asset-free `host_diagnostics` test compiles the production logger with
declaration-only SDL adapters, not a graphical session or live input backend.

## Continuous integration

`.github/workflows/build.yml` checks pushes and pull requests, and supports manual
runs. A private-data policy gate runs before recursive submodule checkout and
builds. Ubuntu 24.04 and Windows Server 2022 (MSYS2 MINGW64) build the complete
application and run CTest without any APK, game assets, graphical session or
credentials for private data.

Successful builds upload `deadspace-<platform>-<commit>` development artifacts,
retained for 14 days. `tools/stage_build.py` creates a **new** staging directory
from an explicit allowlist, includes dependency notices, generates build
information and SHA-256 checksums, and checks the package before uploading.
It never copies tests, saves, logs,
game data or an entire build directory. Linux artifacts require the host
libraries listed in the [Linux install notes](INSTALL.md#installing-linux); they are not universal AppImages.

Actions are pinned to commit IDs, job tokens have read-only contents permission,
checkout credentials are not persisted, and no `pull_request_target` workflow
executes untrusted code. The ordinary build workflow publishes no releases and
builds no installer. A separate published-release/manual-backfill workflow builds
exact-tag portable assets, verifies them, and uploads them with a narrowly scoped
write-permission job. The Windows installer stays manual; see [RELEASING.md](RELEASING.md).

## Source layout

- `src/cpu.*`: dynarmic wrapper, guest threads, host<->guest calls (SVC stubs)
- `src/memory.*`: 4 GiB guest address space, heap, mmap regions
- `src/elf_loader.*`: loads/relocates the ARM shared library, symbolizes addresses
- `src/hle/`: bionic libc, stdio, printf/scanf, pthreads, liblog, GLES 1.1 -> OpenGL, timing
- `src/jni/`: JNIEnv/JavaVM tables and host versions of the Java classes the game calls
- `src/apk.*`: APK (zip) import
- `installer/`: the Windows installer / uninstaller
- `src/render/native.*`, `src/lights.*`: the new renderer: lighting, shadow maps, post-processing
- `src/texpack.*`: texture packs
- `src/input.*`, `src/menu_nav.*`: keyboard, mouse and controller; menu navigation
- `src/sbin.*`, `src/game_patches.*`: patches the game's menu data (SBIN databases): the extra Options
  pages, keyboard / controller wording, the main menu's Quit button
- `src/settings.*`: port settings (`userdata/port_settings.ini`)
- `tools/`: dex disassembler, ARM disassembly helpers, texture/screenshot utilities
  (`tools/disas.py [--lib LIB.so] [-s] SYMBOL` disassembles a function of the game library, `-s` names the
  targets of its calls, and `--grep REGEX` lists symbols; use it to check object layouts before the port relies
  on them. It needs `pip install capstone` and binutils' `readelf`/`c++filt`)

## Commit attribution

Commits made for this fork should name David as author with
`5838258+dafioram@users.noreply.github.com`. AI-assisted commits may include a
`Co-authored-by` trailer; do not replace the user's authorship.
