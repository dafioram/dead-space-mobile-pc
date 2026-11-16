# Android APK / asset compatibility inventory

Status: **read-only survey**, not a new importer or runtime compatibility patch.
Maintained on `feat/android-asset-support`, based on production main `9ede69c`.
Three of the user's four proposed Android APKs have been assessed so far. Do not
infer the architecture or game-core version of the remaining APK from its filename.

No APK, extracted library, asset, save, screenshot, recording, dump, log or private
inspection fixture belongs in this branch, tests or release packages. This document
contains only provenance/compatibility metadata. Supplied copies are inspected
without importing them, modifying the installation or executing the game. All
interactive gameplay/persistence testing is reserved for the user.

## Running inventory

| ID / label | Reported version / package | Game-core architecture | Assets | Native library | Assessment |
| --- | --- | --- | --- | --- | --- |
| A | 1.2.0 / 1200; `com.eamobile.deadspace_full_azn` | ELF32 ARM (`EM_ARM=40`), `lib/armeabi/libDeadSpace.so` | Group A, 1,731 files; identical to installed baseline | Library A; identical to installed baseline | Existing baseline data/layout, not an additional asset-support target |
| B | 1.2.0 / 1200; same package | ELF32 ARM (`EM_ARM=40`), same path; no ARM64 library found | Group A; all 1,731 files match APK A by SHA-256 | Library B; same layout/symbols, nine changed bytes in CPU detection | Same asset support; native-library variant is a static-layout-compatible candidate, **not independently runtime-tested** |
| C | 1.2.0 / 1200; same package | ELF32 ARM (`EM_ARM=40`), same path; no ARM64 library found | Group A; all 1,731 files match APK A by SHA-256 | Library A; identical to installed baseline | Same game-core/asset support as baseline; Android Java/manifest/resource changes are separate from PC runtime support |

APKs A/B report minimum SDK 7 and target SDK 14, with `.DeadSpaceActivity`.
APK C retains minimum SDK 7 and that activity but targets SDK 25 and adds Android
maximum-aspect metadata; its compile/platform metadata also identifies SDK 25.
The package name and Amazon-related entries identify an Amazon-distribution
variant; that is **not** a verification of vendor authenticity, original signing,
license status or freedom from modification. Matching Android version metadata
does not mean two libraries are byte-identical.

### Exact supplied-copy identities

| ID | APK size (bytes) | APK SHA-256 |
| --- | ---: | --- |
| A | 295,945,024 | `0a13b873e19bdf9cd3bfb91537064d1320bcd6c3590253c8e38e6136722def6b` |
| B | 296,056,215 | `88dc0e3d022c5770435a0dc9dd85eb06a5223004f9b4f8d0f7765910037c49f3` |
| C | 325,769,461 | `5167a7491aacbda89e315a7cabb932d8db785440abccb3c0b78a36a21e2a8320` |

| Native library | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| A / C (installed baseline) | 6,672,749 | `2fc87e9a5afa1028d91a9770d23ffd04b45fb5dbc8d3800ed6b31e34223a490b` |
| B | 6,672,749 | `11d718cdf87ed319cd4214dc26284cab422756561683bfb777b61b5a2c1d3b11` |

These hashes identify exact private input copies; they do not authenticate a vendor
release. APK filenames alone should never select a compatibility profile.

## Asset group A

The full set contains **1,731 files / 328,192,023 uncompressed bytes**. Every
decompressed `assets/` file was SHA-256 compared: APKs B/C match APK A, and APK A
matches the installed baseline. No differing or missing asset was found in those
comparisons. ZIP compression/signing/container changes are not asset changes.

Aggregate asset-manifest SHA-256:

`23dfb01fe8d15f42d24fe917a1c87c782a7b1d6a8faeeda4f051cfef6a1a6f25`

For reproducibility, sort non-directory `assets/` entries by their complete ZIP
pathname. Feed each UTF-8 pathname, NUL byte, uncompressed file size as little-endian
u64, and raw 32-byte SHA-256 of its decompressed content into one SHA-256 stream.
This is an inventory fingerprint, not a new save or asset serialization format.

Selected inventory counts:

- 1,373 sound files; 87 model files.
- 151 files under `published/textures`; 36 under `published/texturepacks_ui`.
- 22 map scene files, including test scenes; **not** 22 confirmed playable levels.
- Other particle textures, data, localization, font and layout resources are also
  present. Count/resource names alone do not establish working gameplay features.

All three copies include `.m3g` resources for `fx_linegun_beam`, `lurker_projectile`,
`fx_stasis` and `fx_corer_secondary_fire_beam`. Corresponding `.bin` lookup warnings
therefore do not, by themselves, establish that the entire effect is unavailable.
Whether a particular lookup is an expected fallback or a broken path remains a
separate runtime/asset-loader investigation. Swapping between these identical
asset sets cannot test an asset-content difference as the wall-streak cause.

## Library B: what differs and what does not

The two library files have identical ELF section layout and symbol metadata.
There are **nine changed file bytes**, confined to three instructions inside:

`EA::Audio::Core::DetectCPU::detectcpu()`

The differences lie within offsets `0x2b98f0..0x2b98fb`. In the installed/library-A
variant, the corresponding capability-flag update is disabled; library B retains
the conditional flag update after CPU-info parsing. Read-only inspection locates
the change in audio CPU-capability detection, **not** renderer, mesh, save or
serialization code. The survey does not claim which copy is the original vendor
binary, who changed it or why.

All bytes outside that span match. No broad rebuild, symbol-address shift, new
game architecture or new asset collection was found. This is encouraging for the
existing fixed-address hook profile, but CPU capability selection is executable
behavior: a compatible layout is **not** a completed runtime/audio validation of
library B. Neither library is replaced or patched by this survey.

APK B also differs in signing records (`META-INF/CERT.RSA`, `CERT.SF`,
`MANIFEST.MF`). Signatures/certificates were not authenticated in this quick pass.

## APK C: Android fixes versus game data

APK C contains the **exact same native
game library and every game asset as the installed PC baseline**. Its larger APK
size does not indicate an expanded asset collection: it uses a different mix of
stored/deflated ZIP members (954 stored / 796 deflated, across the whole APK).

Compared with APK A, changes outside `assets/` include Android manifest and Java
DEX content, Android resource tables/layouts, drawable-directory naming and
signing records. The six launcher drawable entries use `*-v4` directory names;
signing entries use `RAFALAMA.RSA` / `.SF` rather than `CERT.RSA` / `.SF`. Those
names do not authenticate an author or vendor. The quick pass does not reconstruct
the Java changes or claim that each Android-side fix is verified.

This is **not** the alternate `assets/guest_libs` layout described in the importer
comment for some other fan-fixed packages. A filename containing "Fixes" is not
enough to choose an importer or texture mode. No new extraction, mesh, renderer,
save or fixed-address-hook implementation is indicated by this APK comparison.

## Current importer boundary

`src/apk.cpp` currently looks for an ELF32 ARM game core in this order:

1. `assets/guest_libs/libDeadSpace.so`
2. `lib/armeabi/libDeadSpace.so`
3. `lib/armeabi-v7a/libDeadSpace.so`

All three surveyed APKs use the second layout, with supported stored/deflated ZIP
entries. Static inspection therefore finds no need for a new extraction-path
rule or asset variant for these three copies. No actual import was run for the
survey. The port's existing original-layout texture-mode policy remains unchanged;
this document is not an independent texture-format audit.

An APK with an ARM64 launcher might still bundle a separate ARM32 game core; an
APK containing only an AArch64 game core would be a different runtime problem.
Inspect every contained ELF and any guest-library payload before calling an APK
a "64-bit game" or assuming it is unsupported/supported.

## Next entries and support decisions

For each remaining supplied APK, append a row with package/version, every native
ABI/core candidate, full APK/library hashes, asset-content group, format/layout
differences, and separate static/import/runtime validation status. Keep Java or
launcher changes separate from native-core changes and asset changes. Do not mix
libraries/assets between versions, overwrite current installations or announce
support solely because filenames or version strings match.

Potential importer/hook changes require their own scoped implementation and
synthetic tests, followed by user-led runtime checks. This inventory authorizes
none of those changes and makes no universal save-compatibility claim.
