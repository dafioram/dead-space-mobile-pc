# Save files: format and what is still unknown

Everything here was checked with `tools/sbin.py` against real files (a port-written profile with one slot, plus the
`.bak` copies the game keeps of the previous version of each file) and, where noted, against the symbol names of the
game library. **Sure** means a file and the tool agree in every case seen. **Inferred** means a good argument but no
test yet. **Unknown** means exactly that. Saves and settings are private user data: never commit them; the tests
build their own databases.

## Files

| File | What it holds |
|---|---|
| `settings.sb` | Port/game settings and which of the three profile slots are in use. |
| `AchievementsSave.sb` | Lifetime statistics, 40 achievement flags and 22 per-level records. No slot number: profile-wide. |
| `NN_LevelSave.sb` | The state at the start of a level (`savetype` is `new_level`). `NN` is the slot, 00..02. |
| `NN_CheckpointSave.sb` | The state at the last checkpoint (`savetype` is `checkpoint`); adds the actor and objective records. |

`settings.sb` has `profile_index`, `profile_active` and `profile00_active`, `profile01_active`, `profile02_active`;
the game library has matching `Settings::setProfileActive`, `isProfileActive`, `setProfileIndex`, `clearProfile`.
Slots 01 and 02 have not been seen yet. The port writes `NAME.bak` (the previous version) before replacing a file, which makes
a free before/after pair: `python tools/sbin.py diff NN_LevelSave.sb`.

## Container (SBIN version 2) - sure

All four files use the same container as `layouts.xml.sb`; `tools/sbin.py` documents it at the top.

- Chunks `tag(4) size(4) hash(4) data`, each 4-byte aligned. The **hash is FNV-1** (32 bit: multiply by `0x01000193`,
  then xor the byte) over the data only. There is no whole-file checksum, so an edit needs the DATA hash fixed and
  nothing else.
- The save data is "kind 1" (unstructured) objects: a typed key/value dictionary. The game library names the pieces:
  `Database::addUnstructuredObjectField`, `removeUnstructuredObjectField`, `getUnstructuredObjectFieldCount`.
  An object is `u16 entry count, u16 byte length` and then the entries; an entry is a field definition stored inline
  (`u16 key string + 1, u16 type, u16 offset of its value from the object start, u16 extra`) followed by the value
  padded to an even size. The length field equals the end of the last value in all 188 objects checked.
- Arrays are kind 2: `u16 element type, u16 struct index, u32 count`, then the elements. Elements are 4-byte aligned
  structs when the element type is 16 (for example `item {type, count}`).
- Values hold object references as object indexes, strings as `string index + 1` (0 is none).

### Value types

| Type | Meaning | Status |
|---|---|---|
| 5 | int32 | sure (the game has `TypeConversion::read<int>` / `write<int>`) |
| 6 | uint32 | inferred: the game has `write<unsigned>`, and the type numbers pair up by size (1/2, 3/4, 5/6, 7/8). Used for `clip`, `upgrade_level_*`, `item_count` |
| 9 | bool / int8 | sure it is one byte (`signed char`); only 0 and 1 seen |
| 10 | float | sure |
| 13 | string | sure |
| 15 | object reference | sure |
| 16 / 17 | struct elements / array reference | sure (used by layouts) |
| 1-4, 7, 8, 11, 12, 18-20 | sizes are in the tool's table; meanings unknown | never seen in a save |

### What the leftover bytes are - sure, and nothing is unexplained

`sbin.py verify --coverage` accounts for every byte of DATA in all four files:

- DATA starts with `00 00 04 00`: an **empty object** (count 0, length 4), a placeholder so no real object sits at
  offset 0.
- Gaps between objects are alignment padding filled with `0xCD` (uninitialised memory; not zero).
- `AchievementsSave.sb` contains **abandoned copies** of its root object, one per key added, each 12 bytes longer than the last
  (1, 7, 8, ... 12 entries). The game reallocated the object each time a statistic was first recorded and never
  compacted the file. The order of the newest keys is the order they first happened. The level and checkpoint saves and the
  settings are compacted (`SaveGame::compact`).
- `AchievementsSave.sb` also has 66 objects nothing points to: 3 per level entry, 22 entries.

`extra` (the last u16 of every field definition, here and in the layout FIEL chunk) is 0 in every entry seen. Its
purpose is **unknown**.

## Vocabulary

Names are exactly as stored. Meanings are what the key name and the game's own function names suggest unless marked.

**Level and checkpoint saves** (same shape; the checkpoint's `actors` and `objectives` are filled, the level save's are empty)

- Root keys: `player`, `dlc_modules`, `tutorials_seen`, `upgrades`, `bench_purchases`, `current_level`, `actors`,
  `objectives`, `triggers`, `misc`.
- `current_level`: `level_id` (string, for example `03_water_02`), `savetype` (`new_level` or `checkpoint`; other values
  **unknown**), `next_actor_id` (counter behind the generated actor names `uan_NNNN`), `cheat_game`, `demo_game`.
- `misc`: `difficulty` (string, `SOFT` seen), `time` and `level_time` (int32 milliseconds; **unknown** whether
  `level_time` restarts per level - both were equal, probably because the test profile jumped levels), `gameCompleted`,
  `nonPlasmaCutterUsed`, `plus` (new game plus).
- `player`: `transform` (12 floats: a 3x3 basis in rows, then the position in the last three - **inferred** from
  `SaveGame::serializeTransform`/`deserializeTransform` and a diff after moving), `hp` (float), `upgrade_level_health`,
  `upgrade_level_o2`, `upgrade_level_rig` (uint32), `telekinesis_available`, `plasmasaw_available`, `tutorial_flags` (int32 bit
  mask, **unknown** bit meanings), `weapons` and `inventory`.
- `player.weapons`: `plasmacutter`, `ripper`, `linegun`, `corer`, `stasis`, each with `clip` (uint32), `available`, `altmode`,
  `current`. `player.inventory`: `credits`, `nodes` (int32) and `items`, an array of `{type, count}` (ammo stacks).
- `bench_purchases`: 6 arrays of `BTN_*` strings (`BTN_CAP1`, `BTN_DMG1`, `BTN_REL1`, `BTN_DUR1`, `BTN_HP1`, ...); the index
  is `Bench::UpgradeType` (`SaveGame::getBenchPurchases`). **Unknown:** which index is which weapon or the suit
  (index 5 held `BTN_HP1`). `upgrades` is 6 empty arrays here; its purpose is **unknown**.
- `actors`: a dictionary keyed by actor name; entries use `destroyed`, `locked`, `state` (int32), `transform`,
  `recreate_type`, `item_type`, `item_count`. The writers are `GameObject::setActorRecordDestroyed`, `setActorRecordOwner`,
  `setActorRecordRecreateType` and `SaveGame::getActorRecord`. **Unknown:** the meaning of `state` values.
- `objectives`: a dictionary keyed by objective name with a `state` of 0, 1 or 2 (**inferred**: inactive, active, done).
- `triggers` is an array; `SaveGame::getTrigger(int, string&, string&, int&)` suggests four fields. Empty so far.
- `dlc_modules` (`markDLCModuleApplied`) and `tutorials_seen` were empty.

**AchievementsSave.sb**

- Counters, int32 each: `kinesisHits`, `limbDismemberments`, `powerNodesFound`, `meleeKills`, `creditsSpent`, `grabBreaks`,
  and arrays `enemyKills`, `enemyHits`, `enemyStomps`, `enemyDecapitations` (9 slots, indexed by the game's `EnemyType`)
  and `weaponHits`, `weaponKills` (7 slots, `WeaponType`). The register calls are `AchievementData::registerKill`,
  `registerHit`, `registerStompKill`, `registerDecapitation`, ... **Unknown:** which index is which enemy or weapon.
- `levelCompletes`: 22 records `{level, difficulty, complete}` (`markLevelCompleted(int, int)`); `difficulty` is 3 in every
  record seen (**unknown:** a sentinel for "never completed" or a difficulty value).
- `achievementCompletes`: 40 records `{name, complete}` (`markAchievementCompleted`).

**settings.sb**: `brightness`, `sfx_volume`, `music_volume`, `sensitivity` (floats), `language`, `profile_index`,
`profile_active`, `profile00_active`..`profile02_active`, `launched_with_active_profile`, `subtitles_enabled`,
`tutorials_enabled`, `auto_reload`, `y_flipped`, `aim_toggle_insted_hold` (sic), `alt_fire_toggle_mode` (int32, 0
seen).

## Still open

1. `extra`; types 1-4, 7, 8, 11, 12, 18-20; whether 6 is really unsigned (edit a value above 2^31 and look at what the game does).
2. The enum values behind `state`, objective states, `tutorial_flags`, `savetype`, `difficulty`, and the achievement index tables
   (`EnemyType`, `WeaponType`). Two ways in: diff a save after one deliberate change, or read the game's own strings
   (`SaveGame::getDifficultyString`, `AchievementData::loadAchievementList`, the actor-record functions).
3. What the game does with slots 01 and 02, with a missing key, and with a type it does not expect.
4. Whether an Android-origin save and a port-written one differ in anything but values.

## Tools

```
python tools/sbin.py dump FILE [PATH] [--types]      tree, or one subtree
python tools/sbin.py diff FILE [OTHER]               semantic diff; FILE.bak against FILE by default
python tools/sbin.py verify FILE... [--coverage]     hashes, lengths, references, unexplained bytes
python tools/sbin.py census FILE_OR_DIR...           key names, types and value ranges over many files
python tools/sbin.py set FILE PATH=VALUE... -o OUT   change scalar values in place (same size; fixes the hash)
```
Work on copies, load them with `--no-save`, and run `verify` before using a file.
