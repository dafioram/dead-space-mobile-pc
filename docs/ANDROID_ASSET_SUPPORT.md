# Android APK / asset compatibility inventory

Status: **read-only survey**, not a new importer or runtime compatibility patch.
Maintained on `feat/android-asset-support`, based on production main `9ede69c`.
Five supplied Android APKs have now been assessed. A–D are direct ARM32 packages
with identical assets. **E (Reanimated 1.3) adds an ARM64 host/JIT wrapper around
the same ARM32 game core, a nested stock asset layout and a separate HD texture
pack.** No supplied copy contains a rebuilt AArch64 game core. Filenames and
Android SDK metadata are not native-architecture or compatibility identifiers.

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
| D — Remastered | 1.2.0 / 1200; same package | ELF32 ARM (`EM_ARM=40`), same path; scan of all ZIP members found no other ELF header | Group A; all 1,731 files match APK A by SHA-256 | Library A; identical to installed baseline | Same game core/assets; Android frontend changes, **not an ARM64 core or remastered game-asset variant** |
| E — Reanimated | 1.3 / 13; `com.deadspace.reanimated` | ELF64 AArch64 host (`EM_AARCH64=183`) plus ELF32 ARM guest | Nested group-A stock subset (1,729 identical files; two INIs absent), plus 222 HD KTX textures and splash images | Guest library A at `assets/game/libDeadSpace32.so`; new ARM64 host `lib/arm64-v8a/libdsr.so` | Genuine ARM64 Android host + additional HD pack; **current PC importer/PNG texture-pack path does not directly support this packaging** |

APKs A/B report minimum SDK 7 and target SDK 14, with `.DeadSpaceActivity`.
APK C retains minimum SDK 7 and that activity but targets SDK 25 and adds Android
maximum-aspect metadata; its compile/platform metadata also identifies SDK 25.
APK D reports minimum SDK 7, target SDK 14, compile/platform SDK 23 and maximum-
aspect metadata. A newer compile/target SDK does not make native code 64-bit.
APK E reports minimum SDK 26, target/compile SDK 35, `.MainActivity`, and Android
native library name `dsr`; its version 1.3 identifies the Reanimated package, not
a newly rebuilt version of the byte-identical stored retail game core.
For A–D, the package name and Amazon-related entries identify an Amazon-distribution
variant; that is **not** a verification of vendor authenticity, original signing,
license status or freedom from modification. Matching Android version metadata
does not mean two libraries are byte-identical.

### Exact supplied-copy identities

| ID | APK size (bytes) | APK SHA-256 |
| --- | ---: | --- |
| A | 295,945,024 | `0a13b873e19bdf9cd3bfb91537064d1320bcd6c3590253c8e38e6136722def6b` |
| B | 296,056,215 | `88dc0e3d022c5770435a0dc9dd85eb06a5223004f9b4f8d0f7765910037c49f3` |
| C | 325,769,461 | `5167a7491aacbda89e315a7cabb932d8db785440abccb3c0b78a36a21e2a8320` |
| D | 325,731,113 | `f3fcad7ea08a762d9f863888901fd31ab8238652494e9f8c02e4d7af138b4bc4` |
| E | 715,366,829 | `dbd94ee54fc3fefdddb101fac4eba1e2058a5db7cfcf30ac41ad96e88b285a3a` |

| Native library | Size (bytes) | SHA-256 |
| --- | ---: | --- |
| A / C / D / E guest (installed baseline) | 6,672,749 | `2fc87e9a5afa1028d91a9770d23ffd04b45fb5dbc8d3800ed6b31e34223a490b` |
| B | 6,672,749 | `11d718cdf87ed319cd4214dc26284cab422756561683bfb777b61b5a2c1d3b11` |
| E ARM64 host, `libdsr.so` | 4,415,408 | `b957561ad19aa65e07dcd09315f3edde917cd50f21accc41ce26cb08ff1c7049` |

These hashes identify exact private input copies; they do not authenticate a vendor
release. APK filenames alone should never select a compatibility profile.

## Asset group A

The full set contains **1,731 files / 328,192,023 uncompressed bytes**. Every
decompressed `assets/` file was SHA-256 compared: APKs B/C/D match APK A, and APK A
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

All four copies include `.m3g` resources for `fx_linegun_beam`, `lurker_projectile`,
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

## APK D: "Remastered" package, ARM32 baseline game core

APK D (Remastered) contains one native ELF:
`lib/armeabi/libDeadSpace.so`, ELF32 ARM, 6,672,749 bytes. A first-header scan of
every non-directory ZIP member found no other ELF, including no AArch64 library
or 64-bit native launcher. The package has no `assets/guest_libs` payload or
alternate native-ABI directory. The native game library is byte-identical to
library A / the installed PC baseline.

All 1,731 decompressed game assets also match group A by SHA-256. There is no
changed mesh, texture, sound, scene or localization file under `assets/`, and no
expanded game-asset collection. The larger archive uses 954 stored / 796 deflated
members, as APK C does; APK size is not evidence of higher-resolution game assets.

Compared with APK A, Android manifest, Java DEX, resource tables/layouts, drawable-
directory names and signing content differ. Compared with APK C, the manifest,
DEX, resource tables/layouts and signing content differ; signing filenames use
`CERT.RSA` / `.SF` rather than C's `RAFALAMA` names. This supports a modified
Android frontend/package, not identification of the modifier or authentication
of a particular fan release. Java behavior and Android-side fixes were not
executed or exhaustively reconstructed.

No new PC importer path, guest architecture, texture collection, mesh format or
fixed-address-hook profile is indicated by this copy. This conclusion concerns
the exact supplied APK, not every download advertised as "Remastered".

## APK E: Reanimated 1.3, ARM64 wrapper and HD textures

This package is materially different from A–D. Its two native payloads are:

- `lib/arm64-v8a/libdsr.so`: a real AArch64 host library. Read-only inspection
  finds Dynarmic A32 JIT (`Run`, register and configuration APIs), guest-thread
  callbacks and JNI/import dispatch machinery. This supports classification as
  an ARM64 host executing/translating the bundled ARM32 game, **not** a recovered
  source-level AArch64 rebuild of that game core. The PC port also uses an ARM32
  guest/JIT architecture; ARM64 Android support and ARM64 game-core bitness are
  different claims.
- `assets/game/libDeadSpace32.so`: the **exact same** 6,672,749-byte ARM32 library
  as the installed PC baseline (library A). Existing game symbol/hook addresses
  therefore have no new stored-core layout to recover from this comparison.

The host can still implement runtime hooks/patches or platform-specific behavior;
identity of the stored guest core does **not** rule those out. The wrapper was not
executed, exhaustively reconstructed or adopted into the PC port, and no author/
vendor authenticity claim is made from names or embedded symbols alone.

### Nested stock assets

`assets/game/assets/` contains **1,729 stock files / 328,188,834 bytes**. Normalize
that prefix to `assets/` and every file matches baseline A by SHA-256. No changed
or additional stock mesh, sound, map, localization or texture file was found.
Only baseline `EAMCore.ini` and `EAMToolManager.ini` are absent from this stock
collection. Their absence is recorded, not silently filled with guessed data or
assumed harmless to every loader.

Normalized stock-subset manifest SHA-256, using the inventory algorithm above:

`05e2cf2f4799bc814b0e84f823b36f4b66169e91915f224f9ca853cd64dc2a5d`

The package also contains three separate splash `.rgba` images. Those are Android
presentation resources, not evidence of a new campaign or replacement geometry.

### HD texture pack

`assets/texture_hd/` contains **222 KTX1 files / 340,222,792 bytes** with 16-hex-digit
filenames, separate from the original stock textures. Header dimensions range
from 64 to 4096 pixels, with full mip chains in the inspected files; examples
include 2048x2048 and 4096x4096. This is an actual additional high-resolution image
collection, unlike the unchanged stock assets in APK D. Image quality, exact
replacement coverage and runtime appearance have not been playtested.

The headers identify 116 files with internal-format tag `0x8d64` (ETC1 RGB) and
106 with `0x87ee` (ATC interpolated-alpha RGBA), compressed 2D single-face images.
All 222 KTX section/mip extents and block-size accounting passed read-only checks.
Those checks validate container layout, not visual decoding, texture semantics,
device format support or correctness of replacement mapping.

HD-pack manifest SHA-256 (same algorithm, keeping `assets/texture_hd/` pathnames):

`ffa18107c93fafd2e62b1286ce3ebeb82a6cb97dfac459bcca37a37696c13b74`

The PC port's `src/texpack.cpp` currently uses **8-hex-digit 32-bit pixel hashes and
PNG replacements**, not this wrapper's 16-digit KTX scheme. Host symbols identify
KTX upload and 16-digit texture naming support. The precise wrapper hash/input and
its relation to PC decoded pixels have not been fully recovered; do not truncate
IDs or assume that converting KTX to PNG alone establishes correct mapping.

## Current importer boundary

`src/apk.cpp` currently looks for an ELF32 ARM game core in this order:

1. `assets/guest_libs/libDeadSpace.so`
2. `lib/armeabi/libDeadSpace.so`
3. `lib/armeabi-v7a/libDeadSpace.so`

APKs A–D use the second layout, with supported stored/deflated ZIP
entries. Static inspection therefore finds no need for a new extraction-path
rule or asset variant for these four copies. No actual import was run for the
survey. The port's existing original-layout texture-mode policy remains unchanged;
this document is not an independent texture-format audit.

E's `assets/game/libDeadSpace32.so` is **not in the current library candidate
list**, so the present importer rejects it before extraction. Merely adding that
candidate would also be insufficient: its game assets must be normalized from
the nested prefix, non-game host/splash/HD files kept out of the stock asset root,
and texture-mode selection handled explicitly. In particular, a game library
located under `assets/` does not imply that its byte-identical baseline textures
were converted from ATC to ETC2. Preserve correct baseline texture behavior rather
than applying the importer's older fan-fixed-layout heuristic to this new layout.

Optional HD support is separate work: establish hash/replacement mapping, support
or safely decode the KTX formats/mip levels, and preserve normal texture-pack
enable/disable behavior. No importer, texture decoder, renderer or installation
was changed during this inventory. Static feasibility is not runtime validation.

An APK with an ARM64 launcher might still bundle a separate ARM32 game core; an
APK containing only an AArch64 game core would be a different runtime problem.
Inspect every contained ELF and any guest-library payload before calling an APK
a "64-bit game" or assuming it is unsupported/supported.

## Survey outcome and future support decisions

The five supplied copies contain **two ARM32 game-core hashes**: A/C/D/E match the
installed baseline; B differs only at the nine-byte CPU-detection change documented
above. A–D share one identical stock asset set; E nests a matching subset and adds
a genuine HD pack plus an ARM64/JIT host library. No new stored game mesh/campaign
collection or source-level AArch64 game-core rebuild was found.

For A–D, no new asset/import layout was indicated. **E is a real additional
importer/optional-HD-support target**, though its guest engine is already the
baseline. Existing baseline-content compatibility must still be distinguished
from independent runtime/audio validation of library B, Android wrapper behavior,
and the unimplemented E importer/HD mapping work. No gameplay was run for these
surveys and none of those implementation changes is claimed complete.

For future APKs, append package/version, every native ABI/core candidate, full
APK/library hashes, asset-content group, format/layout differences, and separate
static/import/runtime validation status. Keep Java or launcher changes separate
from native-core changes and asset changes. Do not mix libraries/assets between
versions, overwrite current installations or announce support solely because
filenames or version strings match.

Potential importer/hook changes require their own scoped implementation and
synthetic tests, followed by user-led runtime checks. This inventory authorizes
none of those changes and makes no universal save-compatibility claim.
