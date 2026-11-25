# Audio and subtitles: formats, runtime logic, and known asset gaps

This note documents how Dead Space Mobile stores and plays dialogue audio and subtitles, what the port repairs in
memory, and which asset/runtime issues are intentionally left for future follow-ups.

## Formats

### `published/sounds/soundBase.sb`
A flat sequential binary table (not SBIN) loaded by `SoundsBase::Init` via `EA::IO::FileStream` (`AssetManager.open`).
Each record is stored back-to-back until EOF:

```
u32 name_len;  char name[name_len];      // NUL-terminated key, e.g. "deadspace/vandal/dem_09"
u32 next_len;  char next[next_len];      // NUL-terminated chained key (len=1, "\0" when empty)
u32 type;                                // 0 = SFX/music pool, 2 = dedicated VO/dialogue slot
u32 mode;                                // 0 = 2D, 1 = 2D positional, 2 = 3D positional
u32 min_range;                           // converted to float (min attenuation distance)
u32 max_range;                           // converted to float (max attenuation / 3D cull distance)
u32 file_count;
  file_count * (u32 path_len, char path[path_len]); // relative to published/sounds/sfx/
```

`SoundsBase::Init` preallocates **460** (`0x1cc`) `SoundObject` slots. The shipped Android `soundBase.sb` contains
**448** records (leaving 12 free slots). Indices **429..442** are treated specially by `SoundsBase::LoadFile` and
`SoundsBase::UnloadAllSFX` as the 14 music tracks, so any appended records must go at index 448+ to preserve all
existing indices.

### `.sps` and `.sph` audio files
Audio clips under `published/sounds/` use EA's SNS/SPS block stream with EA XAS1 4-bit ADPCM (`codec = 0x14`, 22050 Hz,
mono `0x00` or stereo `0x04`):

- **`.sph` (8 bytes):** `codec(1) channels_flags(1) sample_rate_be(2) flags_and_samples_be(4)` where
  `channels = (channels_flags >> 2) + 1` and `num_samples = u32_be & 0x1FFFFFFF`.
- **`.sps`:** Big-endian 4-byte block headers (`type(1) + size_be(3)`):
  - `0x48` (12 bytes): same 8-byte header payload as `.sph`.
  - `0x44`: `u32_be block_samples` followed by interleaved 76-byte XAS1 blocks (128 PCM samples per block per channel:
    4 × 4-byte sub-block headers + 15 × 4-byte interleaved nibble rows).
  - `0x45` (4 bytes): end-of-stream block.

### `published/data/subtitles.sb` and `published/stringdata/<lang>/deadspace.bin`
Both are SBIN v2 databases:

- `subtitles.sb` stores an array of `Subtitle { AudioName, Steps[] }` where each `Step` has `{ ID, timeout }`
  (`timeout` in milliseconds, or `-1.0` for single-step clips that stay up for the sound duration).
- `stringdata/<lang>/deadspace.bin` stores `StringTable` → `StringPair { stringid, string, halversion }`, resolving
  each `Step.ID` to localized text.

## Runtime playback & chaining (`libDeadSpace.so`)

1. Triggers (`SoundEmitter::playSound` or `GameObject::playSound`) call **both** `SoundManager::playSound(key, pos)`
   and `Hud::startSubtitle(key)`. The HUD steps through `Steps[]` independently on its own millisecond timers.
2. `SoundsBase::GetSoundDataByName` looks up records by 32-bit FNV-1 hash (`SoundObject::hashCode`).
3. For **`type == 2`** records (all dialogue and almost all suit announcements):
   - `SoundManager::playSound` uses a dedicated single slot (`sm + 0x60`) separate from the 6-slot SFX pool (`sm + 0x64`),
     stops any previous voice in `sm + 0x60`, copies `SoundObject::next` into `sm + 0xa0` (`nextSoundName`), and starts playback.
   - Each frame, `SoundManager::onUpdate` polls `SoundEngine::IsPlaying(voice, 1.0f)`. Once the active `type == 2`
     voice finishes and `sm + 0x60` clears, if `nextSoundName` (`sm + 0xa0`) is non-empty, it calls `playSound(nextSoundName)`
     to play the next clip in the chain.

## Repaired in memory (Category A)

`src/sound_base_patch.cpp` (hooked via `patches::asset("published/sounds/soundBase.sb")` in `src/game_patches.cpp`)
repairs four multi-step dialogue chains where the shipped Android `soundBase.sb` omitted `next` links or orphan `.sps`
files that are already present on disk:

| Map | Trigger key | Repair applied in memory |
|---|---|---|
| `04_food_01.scene.sb` (Ch. 8) | `deadspace/vandal/dem_09` | Link `next` → existing unlinked `deadspace/vandal/dem_09a` (`sfx/vo/tyler/Dem_15.sps`) |
| `06_titan_01.scene.sb` (Ch. 11) | `deadspace/vandal/dem_06` | Append `deadspace/vandal/dem_06a` (`sfx/vo/tyler/Dem_18.sps`, `type=2`) and link `dem_06.next` |
| `06_titan_01.scene.sb` (Ch. 11) | `deadspace/vandal/dem_07` | Append `deadspace/vandal/dem_07a` (`sfx/vo/tyler/Dem_16.sps`, `type=2`) and link `dem_07.next` |
| `01_gov_01.scene.sb` (Ch. 1) | `deadspace/sabotage/start` | Link `deadspace/sabotage/start_1.next` → existing unused chain `deadspace/sabotage/hell_is_that` (`_1`..`_3`, 4 clips completing the 6-step subtitle) |

Orphan entries are only appended if the corresponding `.sps` file exists in the user's extracted assets and total
records stay within `SoundsBase`'s 460-slot capacity.

## Known remaining items deferred for later (Categories B–D & runtime notes)

### Category B — Map-referenced subtitle keys missing from `soundBase.sb`
- **`deadspace/vandal/tram_01`** (`01_gov_04.scene.sb`, step `Van_29`): Absent from `soundBase.sb`, and there is no
  `TRAM_01.sps` file. However, `deadspace/vandal/tram_07` (`sfx/vo/vandal/TRAM_07.sps`, 1.36 s) sits unused in
  `soundBase.sb` and matches the `Van_29` subtitle line — candidate for a future alias/entry if verified in-game.
- **`deadspace/prologue/intro`** (`01_gov_01.scene.sb`, 7 steps): Absent from `soundBase.sb` with no matching individual
  `.sps` files on disk; appears superseded by the mixed prologue track `deadspace/cinematics/01`.

### Category C — Wrong channel type on a mapped voice line
- **`deadspace/suit/locations/enter_reactor`** (`06_titan_reactor_01.scene.sb`, `suit_09_Now_entering_reactor_01_02.sps`):
  Tagged `type = 0` (regular SFX pool) instead of `type = 2` (VO slot) — the only mapped voice line with `type = 0`.
  Less likely to drop now that all 6 SFX slots are unlocked with voice stealing, but still inconsistent with every other
  suit announcement.

### Category D — Unused / unmapped `subtitles.sb` entries
- `deadspace/tyler/5-1_06`: 8-step subtitle track, missing from `soundBase.sb`, not referenced by any map.
- `deadspace/vandal/1-1_15`: Standalone subtitle entry missing from `soundBase.sb` and unreferenced by maps (the actual
  `1-1_15.sps` clip plays via `deadspace/sabotage/daina_1`).
- `deadspace/vandal/2-1_03`: Present in `soundBase.sb` with `type = 0` and an empty `files` list, not referenced by any map.

### Runtime note — Single `type == 2` VO slot preemption
Because all dialogue and suit announcements share one `type == 2` slot (`sm + 0x60`) and one `nextSoundName` string
(`sm + 0xa0`), a suit announcement (e.g. low health, waypoint updated, quarantine) firing mid-conversation immediately
stops the active VO and overwrites `nextSoundName`, cutting off the rest of a chained conversation while subtitles keep
advancing on their timers. A future runtime fix would need a small VO queue or priority rule in `SoundManager::playSound`.

## Inspection & export tool (`tools/audio_subtitles.py`)

```
python tools/audio_subtitles.py audit ASSETS_DIR
python tools/audio_subtitles.py dump ASSETS_DIR --filter tyler
python tools/audio_subtitles.py export-audio ASSETS_DIR --key deadspace/vandal/dem_09 --chain -o dem_09.wav
python tools/audio_subtitles.py export-audio ASSETS_DIR --key deadspace/vandal/dem_09 --chain --format mp3 -o dem_09.mp3
python tools/audio_subtitles.py export-subtitles ASSETS_DIR --key deadspace/vandal/dem_09 --format srt -o dem_09.srt
python tools/audio_subtitles.py export-subtitles ASSETS_DIR --format txt -o all_subtitles.txt
```

WAV export uses pure Python (EA XAS1 decoder, no external dependencies); MP3 export invokes `ffmpeg` from `PATH`.
