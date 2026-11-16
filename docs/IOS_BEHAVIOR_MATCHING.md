# Deferred iOS gameplay references and behavior-matching checklist

Status: **keep for later; documentation only**. This records future comparison
targets, not authorization to implement IPA import, an iOS runtime, Heavy Pulse
Rifle gameplay or Survival modes. None is supported by the current Android-based
port. See [IOS_ASSET_IMPORT.md](IOS_ASSET_IMPORT.md) for the existing private,
read-only asset findings and the separate inspect/unpack plan.

## Evidence boundary

References were added on 2026-11-16 at the user's request. YouTube oEmbed confirmed
the linked videos' titles and author, **Helios_Max**; search-indexed creator
descriptions supplied the chapter times and run notes below. This pass did not
watch/measure the footage frame-by-frame, execute the supplied package, or perform
a PC/iOS gameplay comparison. Descriptions and user observations are useful leads,
not verified exact simulation rules or proof of a video's executable version.

Retain public links and our own notes only. Do not commit/distribute downloaded
videos, thumbnails, screenshots, extracted game data, archives or private saves.
Use inventory labels for private packages, never their original archive filenames.

## Primary user-supplied references

1. [Dead Space Mobile | iOS | Endless Mode Experience | All Stages](https://www.youtube.com/watch?v=g-SOft1GDEU)
   — Helios_Max. The user reports Heavy Pulse Rifle use and four stages. The
   creator's description calls this roughly ten minutes of Endless gameplay per
   stage, with four stage/result chapter pairs. Treat the weapon behavior as a
   future footage-comparison target, not something implemented or measured here.
2. [Dead Space Mobile | iOS | "5 Minutes To Kill" Mode Run | Waterworks Stage - 813,900](https://www.youtube.com/watch?v=NjnbdU_1eDI)
   — Helios_Max. The title identifies **timed Waterworks**, not Endless. The
   creator describes approximately 90 minutes of play, with gameplay chapter
   **0:24** and results **1:32:18**. This is the supplied long-duration reference;
   do not reclassify it as Endless merely because the run exceeds five minutes.

The long run is consistent with the inspected help text saying kills/combos add
time. **"5 Minutes To Kill" is not evidence of a hard five-minute session limit.**
Measure the initial timer, extension rules and end condition later; do not invent
their numeric values from the mode's name or a video title. The Endless reference
is a separate four-stage showcase, not evidence that a single Endless run lasts
over an hour.

## Four-stage comparison index

The creator's Endless chapter list and four independently titled timed videos
support the same four **stage labels** in both variants. The supplied iOS sample
also contains four Survival scene resources. This does not yet prove a one-to-one
filename mapping, identical geometry/scripts in both variants, or identical enemy
schedules. Verify those links rather than assuming stage order maps directly to
`01_survival` through `04_survival`.

| Stage label | Endless reference: start / results chapters | Helios_Max timed reference |
| --- | --- | --- |
| Miner Threat | [0:18](https://www.youtube.com/watch?v=g-SOft1GDEU&t=18s) / 11:15 | [5 Minutes To Kill — Miner Threat](https://www.youtube.com/watch?v=G8i1yXZZHac) (title score 270,900) |
| The Boom Room | [11:17](https://www.youtube.com/watch?v=g-SOft1GDEU&t=677s) / 25:14 | [5 Minutes To Kill — The Boom Room](https://www.youtube.com/watch?v=ejzxHIIc0oE) (title score 284,520) |
| Waterworks | [25:45](https://www.youtube.com/watch?v=g-SOft1GDEU&t=1545s) / 39:13 | [5 Minutes To Kill — Waterworks](https://www.youtube.com/watch?v=NjnbdU_1eDI) (title score 813,900) |
| No Pressure | [40:06](https://www.youtube.com/watch?v=g-SOft1GDEU&t=2406s) / 51:08 | [5 Minutes To Kill — No Pressure](https://www.youtube.com/watch?v=s9Y-zlWh4b8) (title score 166,960) |

Creator/channel discovery: [Helios_Max](https://www.youtube.com/@Helios_Max).
Chapter boundaries come from the creator's descriptions, not independent frame
annotations; title scores identify recordings, not required deterministic outputs
for our implementation. Add measured timestamps and conditions when this work resumes.

## Behavior-matching work to do later

For every observation record video/time range, mode, stage, difficulty, device,
weapon/upgrades, apparent game speed, inputs and uncertainty. Separate direct
frame/HUD observations from help text, executable names and inferred mechanics.

| Area | What to compare/measure; no current implementation claim |
| --- | --- |
| Stage/mode entry | Both mode selectors, the four stage labels/order, unlock/access conditions, introductions, initial HUD and restart/back flow. Cross-check each label against the actual scene/index record. |
| Endless rules | Enemy spawning/escalation and cadence, score/combo changes and resets, observed health rewards, death/quit/results/retry. Asset help describes combo health rewards; exact thresholds, amounts and timing remain unmeasured. |
| Timed rules | Initial countdown and clock rate, kill/combo time extensions, any cap or decay, pause/resume, timeout/death/results. Match long survivable runs rather than ending an active run at five wall-clock minutes. |
| Shared arenas | Compare layout, spawn points, traversal, doors, hazards and stage-specific scripts between variants; shared labels alone are not proof that every rule/resource is identical. |
| Heavy Pulse Rifle | Acquisition/unlock and inventory mapping; aim/fire/hold/release, cadence, damage/dismemberment, recoil, heat-gauge/cooldown/overheat feedback, any charge/reload behavior, animation, muzzle effects and audio. Names such as charged/active-reload-success in the asset event manifest are leads, not proof of exact input mechanics. |
| Profile/economy | Check the help-described Story/Survival credit sharing and weapon/suit upgrade carry-over, rewards/results/high scores and save/relaunch behavior. Do not assume a video's starting inventory is the default. |
| PC input translation | Later keyboard/controller bindings must preserve native action ownership and the observed weapon/mode timing. A touch prompt or matching icon is not sufficient evidence that gameplay is implemented. |
| Long-session behavior | Long-run scoring/timer bounds, spawn cleanup and CPU/memory trends, without borrowing failure assumptions from the separate Android physics/audio investigations. |

The timed Miner Threat description says the character/tools are fully upgraded
with abundant ammo, and reports a late-run bug where most tools stop damaging
enemies (with named exceptions). The other timed descriptions also mention an
upgraded loadout. Treat this as a **creator-reported anomaly/condition to annotate**,
not the default rifle damage model or a reason to globally suppress/fix damage.
If reproduced later, diagnose it separately before deciding how to handle it.

Do not tune weapon balance or spawn pacing from score totals alone: upgrades,
damage bugs, player decisions and difficulty can change the result. Establish
short, comparable action traces before attempting a long-run acceptance test.
Keep normal behavior, anomalous reference behavior and deliberate port changes
separately identified; never call a visual-only asset import a gameplay match.

## Version and device ledger

| Evidence | Marketing version | Internal build | Confidence / boundary |
| --- | --- | --- | --- |
| Privately supplied iOS sample, plist rechecked read-only on 2026-11-16 | `CFBundleShortVersionString = 1.0.3` | `CFBundleVersion = 1.3.8` | Direct metadata observation; bundle ID `com.ea.deadspace.inc`, minimum iOS `3.0`, device family `[1]`. Not an authenticity/runtime certification. |
| User's possible alternative recollection | Not established | `1.3.11` suggested | No second package or matching metadata inspected. Do not relabel the supplied sample. |
| Public references to a later version | Not established from a package | `1.3.34` is referenced publicly; field assignment unverified | Creator descriptions and a search-indexed [catalog listing](https://www.chip.de/downloads/Dead-Space-iPhone-App_48406551.html) mention this version. No archive, plist, binary or asset comparison performed. The catalog page itself refused direct retrieval in this pass. |

Creator descriptions mention 1.3.34 improving the iPhone 5 experience and an
iPad Mini 1 capture setup, alongside iPad 2 texture/lighting upgrades. These are
**creator-reported build/device claims**, not independently verified capability
rules or proof that every linked recording used the same exact binary. Record
device/quality selection before treating texture/lighting differences as port bugs.

Marketing versions and internal builds are different identifiers: `1.0.3` and
`1.3.8` can describe the same package. Do not identify builds from archive filenames,
mix assets across builds or assume numeric version ordering explains a behavior.
The supplied 1.3.8 sample already contains rifle/Survival resources and executable
names; this pass does not establish that 1.3.34 is required to obtain those features.

If a later legally obtained sample is supplied, inspect its actual plist fields,
bundle/device metadata, executable architecture and identity, asset manifests and
format differences read-only first. Keep identities/inventories private where
needed. Do not download a package merely because a catalog lists it, import it
over an installation, or adopt another runtime as an implicit part of this plan.

## When this branch resumes

1. Reconfirm reference availability/version/device conditions and annotate short
   footage segments for each mode/stage and the rifle.
2. Complete safe inspect/unpack work independently of runtime/gameplay recovery.
3. Pin the chosen sample's type/format/behavior evidence; retain fallback and
   clearly unsupported paths rather than enabling nonfunctional menu items.
4. Implement discrete, reviewable features only after explicit authorization,
   with synthetic tests and user-run behavior comparisons.
5. Confirm timing, failure, results, economy and persistence—not just stage entry
   or an attractive screenshot—before advertising support.

No implementation, release or automated gameplay testing is authorized by this
reference update. This deferred branch is not merged into `playtest` or `main`.
