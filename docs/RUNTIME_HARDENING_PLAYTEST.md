# Allocation/JNI bounds and Unicode-path hardening

This change addresses the three requested correctness areas only. Further
optimization work remains on hold. ELF structure validation, CLI numeric parsing,
GL capability checks, synchronization/JIT changes and unrelated gameplay fixes are
not included. The ELF loader's only change here is native-path file opening.

## Contracts

- Size multiplication and alignment rounding are checked before narrowing to
  guest u32 sizes. Impossible malloc/calloc/realloc requests return null with
  guest ENOMEM; rejected realloc preserves the existing allocation. Valid zero-
  sized behavior, size classes, 16-byte block alignment and allocation strategy
  are unchanged. Actual internal exhaustion remains fail-fast as before.
- Page allocations reject zero/oversized/wrapped sizes before committing memory.
  Unmap validates alignment and the complete rounded range. Invalid guest mmap/
  munmap reports an error rather than modifying allocator/page-table state.
  Permanent data/commit/string paths also validate sizes before arithmetic wraps.
- JNI primitive-array byte products and signed lengths are checked. Region
  validation uses subtraction rather than wrapping start+length sums and checks
  both the array object's extent and external guest-buffer mapping before copies.
  Java exceptions describe invalid JNI inputs. Object-array failure no longer
  proceeds through a null array. String regions/direct-buffer capacity use the
  same bounded approach rather than truncating extents.
- Asset-stream reads validate byte-array offsets/lengths before file reads, so a
  rejected read neither overwrites adjacent mapped data nor consumes input.
  AudioTrack rejects invalid array spans with ERROR_BAD_VALUE before queueing or
  pacing; valid audio writes retain the existing path and timing behavior.
- Host UTF-8 path strings are converted to native filesystem paths/wide Windows
  CRT APIs at file boundaries: assets, game library, logs, PNG/mod discovery and
  dumps, screenshots, debug/shader paths and crash-output destinations. Existing
  transactional profile I/O remains separate; no global locale is changed.
  This is not a new guarantee for every Windows long-path configuration.

## Automated validation

`runtime_bounds_tests` executes the real allocator, C allocation imports and JNI
function-table callbacks with synthetic host registers, not ARM instructions.
Coverage includes the identified product/sum/rounding wraps; invalid requests
leaving allocator state/content unchanged; valid calloc/realloc/map behavior;
JNI exception classes, valid/invalid/empty regions and direct-buffer capacity.
Full app builds also execute actual host Java asset-read/audio callbacks against
synthetic input, without initializing SDL/audio or running the game. Asset-free
app-off sanitizer builds exercise the same primitive JNI and shared span guards.

`host_path_tests` exercises real asset opening, library file opening with a
synthetic rejected non-ELF file, logs, PNG read/write, texture-pack enumeration/
replacement/dump, and stream redirection under non-ASCII directories. No real
library, game image, save/profile, guest CPU/JIT or GL context is used. Existing
profile/import suites separately cover their own Unicode and preservation cases.

## User-only playtest

1. Start the new folder's no-save launcher. Do NOT replace an installed EXE.
2. Load the normal checkpoint, move/play combat, use menus and listen for normal
   audio. Try both Legacy and Native and quit normally. The purpose is to check
   valid game calls are unaffected, not benchmark performance.
3. A short MSAA change is useful to confirm the already-accepted fix still works;
   it is not another optimization experiment.
4. Optional Unicode smoke check: run a COPY of the asset-free test folder under
   a path containing non-English characters. This tests executable-relative logs/
   mod paths. For full game-data/profile-path testing, use PRIVATE copies of the
   complete gamedata and userdata under such a parent and point the launcher's
   path configuration at those copies. Do not move/rename original data to test.
5. Keep `--no-save` enabled. Send the fresh private `deadspace.log` plus any asset,
   texture/audio/menu/crash observations. Temporary progress resets on relaunch.

Do not manually force huge allocations, corrupt libraries or invalid JNI calls
inside a real game session. The synthetic regression tests cover those cases.
No private assets, archives, saves, logs or captures are committed/distributed.
