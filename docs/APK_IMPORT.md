# APK import and isolated tests

No game data is supplied. Provide your own Android APK. This feature changes import
and diagnostics, not the guest engine, physics, audio, renderer or save format.
All interactive playtesting is user-led.

## Scope

The exact supplied A–D packages use `lib/armeabi/libDeadSpace.so` and identical
stock assets. A/C/D contain the installed baseline core; B contains the recognized
CPU-detection variant. Extraction/static identity is **not** an independent
gameplay/audio/save-reload pass for each package. In particular B needs user audio
confirmation. See [the inventory](ANDROID_ASSET_SUPPORT.md) for exact identities.

Import candidates, in preference order:

1. `assets/guest_libs/libDeadSpace.so` (legacy converted ETC2 layout policy).
2. `lib/armeabi/libDeadSpace.so` (original ATC layout policy).
3. `lib/armeabi-v7a/libDeadSpace.so` (same original policy).

The importer verifies ELF32 little-endian ARM shared-library header fields and
logs the selected library's SHA-256. Known core hashes get distinct profile names;
unknown ARM32 cores retain the older generic import behavior with an explicit
**unverified runtime/hook compatibility** warning. Hashes are content identities,
not signature, authenticity, malware or license validation. Filenames, Android
version numbers, signing-resource changes and whole-APK hashes do not gate import.

**Reanimated-style `assets/game/libDeadSpace32.so` is recognized and refused before
destination/staging creation**, with a clear unsupported nested-layout/HD message.
Its ARM64 Android host is not a PC library or evidence of a rebuilt ARM64 game
core. No nested assets, HD textures or Android runtime are partially installed.
Base-layout and optional HD support are separate future tasks; there is no dead HD
toggle. No third-party custom HD implementation has been copied.

## Commands

```powershell
# Extraction only: no SDL, guest execution, profile, default log or crash handler.
.\deadspace.exe --apk "D:\Games\A.apk" --game-dir "D:\DSM-tests\A\gamedata" --import-only

# USER runs this later. --data isolates saves AND port_settings.ini.
.\deadspace.exe --game-dir "D:\DSM-tests\A\gamedata" --data "D:\DSM-tests\A\userdata" --log "D:\DSM-tests\A\deadspace.log"
```

`--import-only` requires both `--apk` and an explicit `--game-dir`; only those
options and `--verbose` are accepted. Redirect stdout/stderr if an import log is
wanted. Exit codes: 0 success/reuse, 1 import failure, 2 invalid arguments.
`--help` still wins without side effects.

Normal `--apk` imports and then starts the game, just as before. Without
`--game-dir`, its destination remains `gamedata` beside the executable. An explicit
`--game-dir` disables fallback to another installation's files and cannot be mixed
with `--assets`/`--lib`. It does **not** change the default profile location: use
`--data` too. Append `--no-save` for disposable progress/settings; omit it to test
persistence in the new isolated profile. No original profile is copied by import.

For each case to also isolate executable-relative logs/crash reports/mods:

```powershell
python tools/prepare_apk_test.py --exe "path\to\deadspace.exe" --apk "D:\Games\A.apk" --case-dir "D:\DSM-tests\A"
```

The helper requires a **new case directory**, copies only the specified EXE,
executes only `--import-only`, and writes `import.log`, `TEST_CASE.txt` and
`Launch.cmd` (`Launch.sh` on Linux). The USER runs the launcher. The first game
launch creates that case's own `userdata`; no progress is migrated. Launchers
accept additional flags, for example `Launch.cmd --no-save`. Failed preparations
leave their owned EXE/log for inspection but no launcher or partially published
game-data directory. Never package or commit case folders: they contain assets,
then private saves/logs. Use separate cases A/B/C/D rather than sharing userdata.

## Failure and existing-data policy

- Extract to a fresh, exclusively created sibling stage; check local/central ZIP
  metadata, method, size, full DEFLATE consumption and CRC before publication.
- Bound central-directory memory to 32 MiB, each selected member to 128 MiB and
  all selected assets to 1 GiB. ZIP64, multi-disk and encrypted members are not
  supported. Stored/DEFLATE and ordinary data-descriptor ZIPs are supported.
- Reject traversal, absolute/drive/backslash/reserved paths, duplicate/case-aliased
  members, selected symlink members and reparse/symlink destination ancestors.
  These are importer protections, not a sandbox against a concurrent hostile
  process changing the same filesystem paths.
- Exclude packaged `assets/var` and `assets/userdata` profiles. No saves are imported.
- Check short/failed writes, flush and close; publish the entire fresh directory
  with a no-replace rename on Windows/Linux. Different/incomplete existing game
  data is refused unchanged, including an empty existing destination directory.
  Choose a fresh path; the importer never deletes the old one.
- Identical existing assets/library/texture mode are compared byte-for-byte and
  reused without touching files, timestamps, import metadata or userdata. This
  comparison uses a fresh stage too, so allow space for one extra import.
- `IMPORT_INFO.txt` records source filename, core hash/profile, texture policy and
  extracted asset count/bytes. It is local diagnostic metadata, not a trusted
  installation manifest. Source ZIP/signing/Java resources are not installed.
- Handled failures clean only the stage owned by that invocation. Hard termination
  can leave `.deadspace-import-*` staging data for manual inspection/removal;
  later imports never delete unidentified old stages. No power-loss durability
  guarantee is made for the asset tree. Original APKs and existing profiles are
  not modified. The installer no longer pre-deletes existing `gamedata` on import.

## User acceptance checklist (each supported package)

1. Confirm `import.log`/`IMPORT_INFO.txt` identify the expected core and ATC mode.
2. Start with that case's launcher; check menus, textures and both renderers.
3. Play normally; check sound effects, dialogue and a scene transition, especially B.
4. Reach a save checkpoint; quit normally, relaunch the **same** case and verify
   progress/settings persist. Do not use `--no-save` for this persistence check.
5. Report case ID, EXE revision, any error and relevant private logs. Do not upload
   APK/assets/saves/dumps to a public issue or attach them to releases.

Synthetic tests cover layouts/compression, SHA-256 vectors, Unicode paths, malformed
ZIPs/CRC, bounded mutations, unsafe names, failed writes/close/publication,
existing-data preservation, early CLI paths and isolated-case preparation. These
tests do not execute synthetic ARM bytes or claim gameplay compatibility.

The main-based implementation passed 16 Windows and 16 Linux CTest suites,
plus 14 app-disabled Linux ASan/UBSan suites. The updated Windows installer source
also passed a compile-only check; no installer was published or executed.
