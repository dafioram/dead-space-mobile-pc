# CPU and memory diagnostics (opt-in, not an optimization)

## Detailed portable playthrough capture

The extended diagnostic branch adds bounded audio cadence/backpressure histograms,
pre/post-write PCM queue ranges with format-derived duration, instrumented ownership
counters, and a process-local Windows handle-type helper. The frame-stage revision
also fixes Windows per-thread timer ownership (normal thread exit closes its own
timer). It does not change the audio queue limit, wait semantics, gameplay, graphics
quality, cache policy or save rules. No Linux input workaround is included.
`tools/playthrough/START-HERE.txt` describes the portable setup and measurements.

With diagnostics enabled, F3 reserves a manual issue marker and dumps up to 180
recent completed frame phase records at the next eligible report (at most once per
second). Reports also retain the worst 16 frames >=50 ms since the previous report;
other threshold crossings are counted. The FPS overlay, if enabled in Graphics,
adds the worst frame in that recent history. This is not GPU timing and does not
identify a function or sound event. Manual dumps themselves incur logging cost.

Windows `handle-census.exe` uses process-local `ProcessHandleInformation`, not a
system handle scan. The external watcher invokes it approximately every 30 seconds
for only its own child's PID and exact creation time. It duplicates a bounded set
of representative handles into the helper solely to query object **types**, checks
the duplicate's type index against the captured bucket to detect reuse, and closes
its own copies. It never queries object names, requests debug privileges, changes
target handles, suspends/terminates a process or captures game memory. Unsupported
queries produce explicit invalid/error rows, not a zero-handle or leak claim. The
helper's own handle table is queried only to verify those duplicates' type indexes.

Owner gauges count host wrapper objects/references, not necessarily one kernel
handle each. JNI and I/O registry snapshots use try-lock and explicitly report
unavailable/busy data. Windows timers now use a noncopyable thread-local owner. It
preserves high-resolution creation flags, one-shot waits, scaling, rounding and
Sleep fallback; only normal thread-exit ownership changes. No thread closes another
thread's timer. `wait_timer_closed` counts successful closes, and
`exited_thread_timer_retained` now counts failed closes only (a semantic change from
the previous observer). Create/arm/wait/close failures and last Windows error codes
are explicit. The existing wait-error return semantics are unchanged. The final
report precedes shutdown and may include active threads' timers; process `_Exit`
leaves their ultimate reclamation to Windows, as before.
Queue times exclude hardware/backend latency. Histograms are fixed-size and log
disjoint upper-bound ranges, not exact percentile samples. No PCM is collected.

## Original reporting and interpretation

### Frame-stage revision (schema 4)

`stages_mean_ms`, `stages_max_ms` and `stage_calls` report bounded, exclusive
**main-thread wall** buckets. Each hitch and F3 history record carries the same
frame's individual bucket values and scope counts:

| Bucket | Scope |
| --- | --- |
| `guest_hle_other` | `NativeOnDrawFrame` remainder after instrumented Native children; includes uninstrumented guest/JIT, HLE, Legacy rendering and driver work, **not pure update/JIT CPU time** |
| `native_other` | Native draw/index preparation and frame bookkeeping not assigned below |
| `vertices`, `normals` | Existing CPU vertex preparation and normal generation |
| `scene_cache` | Caster classification/hashing/transforms, still-scene insertion/invalidation and scene reset |
| `fixtures` | Lamp scanning/facing, frame-light selection, beacon/glow/fan-floor work and retained lighting texture pixels |
| `shadows` | Shadow-frame setup, atlas assignment/baking and moving-mesh shadow draws, excluding instrumented children |
| `upload_draw` | Native GL state, vertex/index uploads and draw submission; excludes instrumented shadow children |
| `post` | Native post-processing, including required resolves and initialization |
| `present`, `swap`, `limiter` | Final present call, SDL swap call and existing frame limiter |
| `host_other` | Remaining main-loop work, including input, overlays and uninstrumented guest callbacks outside `NativeOnDrawFrame` |

Nested buckets subtract child time from their parents; their sum covers the
measured loop. Valid-frame/error counters expose coverage or scope errors. Counts
are scope entries, not GPU draw counts; one displayed frame may run multiple guest
frames during cinematic skip/turbo. Means cover all interval frames. Per-bucket
maxima need not occur in the same frame and must not be summed. A blocking driver
call stays charged to its caller: these measurements do not separate running CPU,
descheduling, driver waits or GPU execution. Fine Native stages do not profile the
Legacy fixed-function path. There are no GPU queries, fences or added `glFinish`.

Loop counts use `>25 ms`, `>33,333,333 ns` and `>=50 ms`; work counts use
`>16,666,667 ns`, `>25 ms` and `>33,333,333 ns`. These overlapping counts cover
**all** interval frames, not just the retained percentile ring. Work ends before
swap and limiter. A 30 FPS cap naturally exceeds the 25 ms loop budget; focus,
loading, fast-forward and quality must accompany any comparison.

`between_ms` is the preceding loop-end to current loop-start gap, including the
preceding sampler/logging and scheduling. `start_interval_ms` and
`swap_call_interval_ms` are intervals between consecutive loop starts and swap
**calls**, respectively, so they cross a frame boundary. They are not measured
display presentation cadence. The original loop time still excludes the sampler.
SDL version/video backend, current display mode/refresh (with validity), drawable
size and SDL-reported swap interval are queried at reports, without changing them.
The driver/compositor can still determine actual presentation behavior.

Still-scene live/dead counts are updated at every existing insertion,
invalidation and scene reset, including beyond the old 4,096-entry scan limit.
Lifetime totals retain inserts, live/dead lookup hits, invalidations, insertion
refusals, reset counts and cleared live/dead entries. `scene_accounting_valid`
checks live+dead against actual container size. Upload totals are submitted bytes,
not current VRAM allocation. Refusals include the existing capacity/index
preconditions. Nothing is evicted or reclaimed by these counters; dead entries
and vector capacity retain their existing lifetime policy.

Clock reads and scope bookkeeping add per-draw overhead, not just report cost.
F3 emits up to 180 longer lines and may itself cause a pause: the next frame's
between-frame measurement exposes that gap. Use the same EXE without `--perf`
for a diagnostic-overhead comparison; the timer fix remains active in both runs.

Start with `--perf` to add bounded `[perf]` reports to the normal private game log.
For a full playthrough, `--perf-interval 10` enables the same reporting every ten
seconds (accepted intervals: 1, 5, 10, 30 or 60 seconds). Frame/event storage remains
bounded. New native scratch and GLES conversion size/capacity counters distinguish
retained buffers from contents. Native still-scene live/dead accounting is now exact
at insertion/invalidation/reset sites; texture payload scans remain capped at 4,096.

`tools/playthrough/` supplies a user-run Windows kit launcher with its own persistent
`userdata`, a fresh report directory per run and an external process-only memory
CSV every five seconds. This setup deliberately omits `--no-save` so progress in
that separate profile survives. Read its `START-HERE.txt` before copying a profile.
`--crash-dir DIR` puts best-effort crash dumps and stderr in the existing selected
directory; normal default behavior remains beside the executable. On Windows the
dump is attempted before normal crash logging/symbolization. This cannot guarantee
capture of hangs, forced termination, logger deadlocks in first-chance reporting,
driver failures or severe exhaustion. No process is automatically stopped.

Earlier playthrough-kit synthetic validation on Windows: all 24 CTest suites passed,
including real crash-reporter abort/exception dumps into Unicode report paths and
the shipped watcher run against an asset-free fixture (5-second samples, quoted
paths, persistent test progress, separate reports and nonzero child exit status).
All 19 asset-free Linux suites passed. These checks do not establish gameplay
stability, real-world diagnostic overhead or leak/cache-eviction conclusions.

Disabled by default; startup-only, before guest allocation/thread creation. This
does not change frame caps, sleeps/spins, gameplay, cache eviction,
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

Sampling occurs at the chosen interval (one second by default) **on a completed main frame** and once before
shutdown. No diagnostic thread is created and no cache is evicted. If the main
thread blocks, reports can pause too; use an external sampling profiler to locate
a stuck or CPU-heavy stack. Events are capped at 32 pending entries, with drops
reported. Report lines are split below the existing logger's buffer limit.
Diagnostic report exceptions stop that sampler, not game execution; ordinary
native/game failures are neither caught nor suppressed by it.

Counters for allocations/JIT/thread lifetime add small startup-opt-in bookkeeping
costs. Disabled builds still contain lightweight gates; there is no runtime enable
because earlier allocations would otherwise be missed. No optimization or leak
fix beyond scoped Windows timer ownership is claimed. Follow-up CPU sampling can
distinguish frame-spin, native host,
guest/JIT and driver work; bounded logs alone cannot name a hot guest instruction.

Synthetic tests exercise percentiles/overflow/reset, allocation/realloc/free and
map/decommit accounting, concurrency, event bounds/sanitization, OS query lifecycle,
JIT bookkeeping, actual report formatting through stand-in adapters, disabled
behavior and sampler-only failure containment. No game session is automated.
