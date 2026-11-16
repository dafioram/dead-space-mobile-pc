# CPU and memory diagnostics (opt-in, not an optimization)

Start with `--perf` to add bounded `[perf]` reports to the normal private game log.
Disabled by default; startup-only, before guest allocation/thread creation. This
does not change frame caps, sleeps/spins, gameplay, native cleanup, cache eviction,
texture contents, graphics quality, saves or failure handling. Instrumentation
has overhead; compare against the same EXE without `--perf`.

All gameplay is user-led. Agent validation uses builds and synthetic tests only.
No game data, save contents, images, audio or memory dumps are captured by this
feature. Logs still contain normal game/profile metadata: keep them private, never
commit them or include them in a release.

## Safe setup

Use a separate diagnostic EXE folder; do not overwrite the installed executable.
Point it at the existing matching assets/library and use a disposable profile:

```powershell
.\deadspace.exe --assets "D:\Game\gamedata\assets" --lib "D:\Game\gamedata\libDeadSpace.so" --data "D:\Game\userdata" --no-save --width 1280 --height 720 --fps 60 --perf --log ".\perf-run-01.log"
```

The USER runs that command. `--no-save` permits normal temporary autosaves/settings
but does not write the source userdata. Relaunch starts from the source again;
it is not a persistence test. Close any other instance normally before preparing
the disposable copy. Keep the original saves and complete checkpoint/level pairs.

With separate `--assets`/`--lib` paths, explicitly match the texture mode too:
append `--atc-textures` for original-layout ATC data (A–D), but not converted ETC2
data. Those separate paths do not select an external game-data marker themselves.
The combined playtest's prepared launchers instead use `--game-dir` to select the
whole installed game-data root and its existing texture-mode marker automatically.

For the control, run the identical EXE/arguments without `--perf`, with a fresh log
name. Every log filename must be new: the existing logger truncates a reused name.
Use the same resolution, 60 FPS cap, VSync setting, scene and camera. A smaller
window by itself is not proof that the internal render resolution changed.
The provided private diagnostic/control launchers use fresh log directories.
No texture pack is copied into the separate test EXE folder; that is a clean
stock-texture baseline, not a benchmark of installed mods.

## First test (roughly 10–15 minutes)

Open the console with backtick (`~`). `/perf status` confirms the feature is on.
`/perf mark LABEL` adds a read-only label; close the console before each measured
idle period. Labels accept letters/numbers/underscore/hyphen and are capped at 48
characters. Markers are logged with their age at the next sample, not necessarily
the exact line timestamp at which Enter was pressed.

1. **Title:** mark `title_idle`, close the console, wait 60 seconds.
2. **Gameplay:** load a known checkpoint normally. Wait 30 seconds for initial
   loading/cache warm-up; mark `game_idle`, stand in one place for 120 seconds.
3. **Pause:** mark `pause`, then pause normally for 60 seconds.
4. **Background:** minimize for 60 seconds, then restore and wait another 30.
   Focus/minimized flags and window events are recorded. The diagnostic build does not add throttling.
5. **Repeated load:** return to the title/menu and reload the **same checkpoint**
   five times without exiting the process. Mark `cycle_1` through `cycle_5` after
   each load, then wait at least 30 seconds in the same spot each time. If the game
   does not unload/reconstruct the world on that route, the log will reveal it;
   do not assume every menu visit is a map transition.
6. **Real transition:** if convenient, play across a normal map boundary and back.
   Do not use console level jumps, spawning or cinematic fast-forward for the
   initial baseline. Loading and world-construction/retirement requests are logged
   automatically; an entry hook is not proof that every native resource is freed.
7. **Quit normally.** Preserve the full log, even if you saw no issue.

Keep geometry capture, verbose GL/input tracing, screenshots, audio/texture dumps
and developer turbo off during this baseline. Do not change graphics settings
mid-comparison. Tell us if other programs were busy or any overlay/recording was on.

Then repeat at least the stationary gameplay section with the control launcher,
checking Task Manager CPU/memory with both runs. This checks diagnostic overhead.
For renderer comparison, use fresh runs of the same checkpoint and select Native
versus Legacy (F8 or Graphics); otherwise keep settings/camera identical. Mark
which renderer you selected and allow 30 seconds after a switch. In-process
switching can warm shared caches, so it is not a cold-memory comparison.

Send the complete logs privately, your approximate test sequence, renderer and any
stutter/crash observations. Do not send the asset/save folders as a public issue
attachment. A crash/frozen main thread may stop these reports; retain the existing
crash evidence rather than trying to repeatedly force the same failure.

## Reading the reports

- **Process:** Windows private bytes are committed private process memory;
  resident/peak resident are working-set values. Peak resident does not decrease.
  Linux provides RSS/high-water RSS here; private bytes are explicitly unavailable,
  not a zero-memory claim. The 4 GiB guest address reservation is separate from
  physical memory. CPU uses **100% = one logical core**, and can exceed 100% for
  the whole process; Task Manager usually scales against all logical processors.
- **Guest heap:** live block bytes/count, peak live bytes, committed bytes and
  committed-minus-live (free/reusable or unused space). Blocks include allocator
  rounding. A free lowers live bytes without necessarily returning committed
  memory to the OS. Accounting errors indicate diagnostics need investigation.
  All-guest mapped bytes additionally cover mapped stacks/library/permanent pages,
  not host JIT allocations. Counts come from existing allocator/page bookkeeping,
  not a scan of guest heap contents.
- **Caches:** selected CPU texture/buffer payload sizes and retained capacities;
  scene mesh/node/fade/light observations; PNG replacement/miss entries. These are
  partial, overlapping views, not a sum that must equal process private bytes.
  Container overhead, unobserved allocations and driver memory are not fully
  counted. Each payload-map scan is capped at 4,096 entries; truncation flags mean
  reported scanned bytes are only a lower bound. Clearing vectors can leave
  capacity available for reuse.
- **Threads/JIT:** live registered guest-thread query handles/clock IDs and JIT
  instances/configured cache bytes. Configured JIT bytes are not measured resident
  executable bytes. Up to eight hottest surviving guest threads are identified by
  OS thread ID; exited threads/driver/SDL workers are not individually covered,
  though process CPU includes them. Registration generations prevent OS ID reuse
  from being mistaken for continuous thread activity. Queries cap at 64 threads.
- **Frame loop:** mean/p50/p95/p99, CPU-wall work, nested game/render wall time,
  swap blocking, limiter sleep and deliberate limiter spin. Work includes game/
  render time (do not add the two). These are wall intervals, **not GPU timings or
  exclusive CPU samples**. Loop timings exclude the sampler; its elapsed cost is
  logged separately, including diagnostic logging up to that line. Interval FPS
  and process CPU include time spent sampling. The first sample is a baseline.
  Means cover all interval frames; percentiles/max use at most the last 4,096
  frames and report overwrites. Cap/VSync/quality flags describe requested/current
  settings, not guaranteed display cadence or driver-enforced VSync. Fast-forward
  and turbo are explicitly identified.
- **Graphics estimate:** level-zero RGBA-equivalent texture dimensions describe
  tracked metadata only. They exclude mip/MSAA/shadow/driver allocations and are
  **not actual VRAM usage**. This build makes no GPU-time or complete GPU-resource
  accounting claim and introduces no `glFinish`, readback or GPU synchronization.
- **Audio:** latest observed queue size, backpressure threshold and existing
  underrun count. The queue sample is from the last write, not an instantaneous
  measurement at the report timestamp.

Memory may peak while both scenes coexist. Look after settling, and compare the
same warmed state across repeated cycles. Growing *live* blocks/resources is more
concerning than stable retained capacity. A single increase does not prove a leak.
Independent snapshots are not one atomic cross-thread view of the entire engine.

## Scope and limits

Sampling occurs once per second **on a completed main frame** and once before
shutdown. No diagnostic thread is created and no cache is evicted. If the main
thread blocks, reports can pause too; use an external sampling profiler to locate
a stuck or CPU-heavy stack. Events are capped at 32 pending entries, with drops
reported. Report lines are split below the existing logger's buffer limit.
Diagnostic report exceptions stop that sampler, not game execution; ordinary
native/game failures are neither caught nor suppressed by it.

Counters for allocations/JIT/thread lifetime add small startup-opt-in bookkeeping
costs. Disabled builds still contain lightweight gates; there is no runtime enable
because earlier allocations would otherwise be missed. No optimization or leak
fix is claimed. Follow-up CPU sampling can distinguish frame-spin, native host,
guest/JIT and driver work; bounded logs alone cannot name a hot guest instruction.

Synthetic tests exercise percentiles/overflow/reset, allocation/realloc/free and
map/decommit accounting, concurrency, event bounds/sanitization, OS query lifecycle,
JIT bookkeeping, actual report formatting through stand-in adapters, disabled
behavior and sampler-only failure containment. No game session is automated.
