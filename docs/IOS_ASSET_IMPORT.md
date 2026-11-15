# iOS asset import: findings and staged plan

Status: investigation only. The port does **not** currently import IPAs, load an
iOS executable, or support the iOS-only rifle and Survival assets. This document
starts the isolated `feat/ios-asset-import` branch from production main, not from
the combined playtest branch.

## Scope and evidence

A quick, read-only inspection on 2026-11-15 examined one user-supplied IPA named
`Dead_Space__1.0.3_ios_3.0.ipa`, comparing its asset filenames with the local
Android installation. The archive was not executed or imported into the game.
The inspection covered ZIP entries, bundle metadata, selected serialized tables,
an audio-event manifest, and printable executable names/symbols. It did not
disassemble gameplay, validate rendering, or test unlocks and saves.

Bundle metadata reports:

- Display name: Dead Space; identifier: `com.ea.deadspace.inc`.
- Marketing version: `1.0.3`; internal bundle version: `1.3.8`.
- Minimum iOS version: `3.0`; device family: iPhone (`1`).
- Bundle root: `Payload/Dead Space.app/`.
- Asset roots: `published/` and `published_low/`.
- Executable: `Dead Space`, with a universal Mach-O header, not an Android ELF
  shared library. Architecture slices and encryption status were not investigated.

These observations apply to this sample and our installed Android package, not
every iOS or Android release. Metadata is not proof of archive provenance.

## Content that stands out

| Feature | Evidence | Limit of the finding |
| --- | --- | --- |
| Heavy Pulse Rifle | `models/burst_rifle.bin`, `models/burst_rifle.m3g`, rifle textures, muzzle/vent effects, and heat-gauge graphics; executable contains `WeaponBurstRifle` names/symbols | Stronger evidence than Android's UI remnants; behavior and import compatibility untested |
| Rifle audio | Dedicated shot-end, charged, warning, fire-loop, active-reload-success and overheat events in `sounds/deadspace.txt` | Event names do not establish exact mechanics or sound-bank compatibility |
| Survival arenas | `maps/01_survival.scene.sb` through `04_survival.scene.sb`, registered in `data/level_index.sb` | All four are absent from our Android installation; playability in this port untested |
| Endless and timed Survival | Help/UI tables plus `GameModeSurvival`, `EndlessSurvivalResults` and `TimedSurvivalResults` executable names | Endless and **5 Minutes to Kill** are Survival variants, not three independent categories |
| Chapter Select | Help/UI tables and `ChapterSelectWindow` executable names | Help describes replaying completed chapters without saving progress; not tested |
| Nightmare and Nightmare New Game+ | Difficulty and unlock text | Availability, unlock conditions and gameplay rules not verified |

The Survival arena labels are **Miner Threat**, **The Boom Room**,
**Waterworks**, and **No Pressure**. Help describes credits earned in Survival
being usable in Story, Story weapon/suit upgrades carrying into Survival, combo
health rewards in Endless, and kills/combos adding time in the timed variant.
These are descriptions in the supplied data, not gameplay confirmations.

The iOS model directory has 89 files versus Android's 87; the two additional
filenames are the rifle's model pair. Its map directory has 27 files versus
Android's 22: the four Survival maps plus `00_test_respawn.scene.sb`. All Android
map filenames are present in the IPA; no obviously additional campaign chapter
was identified. Shared filenames do not imply identical content. The test map
is not evidence of another player-facing mode.

Android already retains rifle names/descriptions, help text, `BTN_BURST` and
`ICON_BURST` layout entries. Its inspected library's weapon classes and native
item mappings did not reveal a rifle implementation. Merely enabling a button
or granting an inventory entry is not a supported way to recover the weapon.
Other shared mode/difficulty text likewise does not establish Android support.

## Compatibility boundaries

- `src/apk.cpp` currently imports Android assets and requires a 32-bit ARM
  `libDeadSpace.so`. `src/elf_loader.cpp` loads that ELF library. A ZIP-compatible
  IPA container does not make its Mach-O executable loadable by this path.
- iOS uses FMOD `.fev`/`.fsb` sound banks, unlike the installed Android package's
  individual sound files. Audio conversion or a separate integration decision
  is needed; copying these banks is not sufficient.
- English text is under `stringdata/ENG_US/deadspace_iphone.bin`, rather than
  Android's `stringdata/en-us/deadspace.bin`. Paths and language mappings differ.
- The inspected iOS level index is SBIN v2 and readable with `tools/sbin.py`.
  This does not prove every database, model, texture or scene is interchangeable.
- Texture encoding, low-detail asset selection, animation compatibility, save
  compatibility and all runtime integration remain unverified.
- Asset extraction, asset conversion and gameplay support are separate stages.
  Reconstructing missing gameplay within the Android-based port is a different
  task from supporting an iOS executable; neither is implemented here.

## Proposed first implementation: inspect and unpack only

1. Extend private-data guardrails before introducing extraction. Explicitly
   reject IPAs, iOS app bundles, FMOD banks and retail Mach-O content in commit
   and package checks; add appropriate ignore rules. Existing checks are a
   safety net, not a guarantee that arbitrary iOS files are detected.
2. Add a developer-only inspect/unpack tool for a user-supplied IPA. Identify
   the app using its bundle metadata, report version and asset roots, and make
   clear that extracted content is **not runnable** in the current port. Do not
   introduce a gameplay `--ipa` option that implies support prematurely.
3. Unpack only explicitly selected asset roots to a new, private destination
   outside the checkout, installation and userdata. Preserve `published/` and
   `published_low/` separately. Do not copy the executable, account/social data,
   or unrelated bundle resources as game assets, and never overwrite Android
   `gamedata/`. Keep any provenance/inventory manifest private beside the output.
4. Validate the archive before writing: reject ambiguous app roots, unsafe or
   absolute paths, traversal, links, duplicate/case-colliding targets, encrypted
   or unsupported ZIP members, excessive counts/sizes and corrupt payloads.
   Verify CRCs, fail visibly on write/cleanup errors, and do not silently leave
   an incomplete extraction looking successful.
5. Test inspection and extraction using generated miniature ZIP/plist fixtures
   only, including malformed archives and untouched-source/destination checks.
   Keep all real-package checks local and separate from CI/release artifacts.
6. After extraction works, inventory format compatibility without replacing
   live assets. Treat rifle integration, Survival modes and any content unlock
   as explicit later work with separate gameplay confirmation.

No IPA, extracted content, executable dump, save, log, screenshot or private test
fixture belongs in Git, CI artifacts or release packages. Use only a user's own
legally obtained content. The APK import path and ordinary playtest workflow
must remain unchanged. See [DEVELOPMENT.md](DEVELOPMENT.md) for asset policy and
synthetic-test requirements.
