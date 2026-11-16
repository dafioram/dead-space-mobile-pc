# Frame-wait CPU experiment (not a gameplay/rendering optimization)

This branch changes only the host frame limiter's waiting method. It does not
skip native updates/rendering, pause on focus loss, change the frame cap/deadline,
quality, audio, saves, allocator/cache/JIT budgets, or native failure handling.
No RAM reduction or higher uncapped gameplay FPS is expected or claimed.

## Implementation and fallback

`--frame-wait timer` (default) uses one main-thread-owned Windows high-resolution
auto-reset waitable timer, waiting until approximately 0.2 ms before the existing
SDL performance-counter deadline and spinning only for the remainder. It does
not alter process-wide timer resolution or thread priorities. The original cap,
fast-forward/turbo bypass, and late-frame resynchronization remain unchanged.
The timer is closed explicitly before the existing `_Exit` shutdown.

`--frame-wait legacy` uses the original SDL delay (whole milliseconds minus one)
and final spin. Unsupported systems (including Linux) and timer creation/arm/wait
failures fall back to that path, not a coarse waitable timer. Startup logs identify
requested/actual backend; a runtime failure logs once and disables that timer.
An unavailable timer makes the A/B comparison inconclusive, not a successful gain.

Scheduler wakeups may miss the margin, especially under load. This is an
experiment: compare frame tails and input feel, not just average FPS/CPU. When
VSync already consumes the interval or rendering exceeds budget, little or no
benefit is expected. Background/minimized behavior has NOT been redesigned.

## User-only A/B test

Use the separate, asset-free test folder, never replace the installed EXE. The
private prepared launchers use the same executable, existing complete game-data
root/texture-mode marker, original profile as a read-only source, disposable
`--no-save`, 1280x720, 60 FPS, and `--perf` in BOTH modes. Each launch gets new logs.
No original save/settings/installation is modified. Temporary autosaves work but
are discarded on exit; relaunch starts from the original checkpoint again.

1. Close other game sessions normally yourself. Run the **legacy-wait** launcher.
2. Choose Legacy or Native explicitly; disposable settings reset between launches.
   Keep renderer, VSync, MSAA, resolution, camera and checkpoint identical in both
   modes. Don't mix renderer switching with a timed comparison.
3. Load the same checkpoint, settle for 30 seconds, enter `/perf mark idle_start`,
   close the console, stand at the same viewpoint for 90–120 seconds.
4. Enter `/perf mark gameplay_start`, then play the same short route/combat section
   for several minutes. Note input feel, stutters, audio, menus and transitions.
5. Quit normally. Repeat with the **timer-wait** launcher. If convenient repeat in
   reverse order to reduce warm-up/background-load bias. No minimizing is required.
6. Send each complete `deadspace.log` privately with mode/renderer and observations.
   `console.log` is usually a duplicate. Logs and profiles are not public artifacts.

Compare settled CPU (100% = one logical core), limiter spin/sleep, FPS and
loop p95/p99/max. Work/game-render and swap are wall time, not GPU execution.
Less spin with similar CPU-wall work is expected; lower process CPU without worse
tails/feel is the acceptance criterion, not guaranteed by synthetic tests.
Private commitment/working set should be broadly comparable, not guaranteed equal.
For actual-gameplay optimization afterward, compare the marked gameplay sections
separately from idle/menus/loading, then attribute the hot thread/stacks before
selecting a renderer/guest/JIT change. Do not infer a leak from one memory step.

`--perf` remains startup-only diagnostics; an instrumentation-off control is a
separate comparison. See `PERFORMANCE_DIAGNOSTICS.md` for the counter boundaries.

## Asset-free validation

`frame_wait` tests the production wait policy with injected clocks: exact legacy
equivalence, guard/unit conversion, early/late wakes, timer failure after elapsed
time, deadline completion, disabled diagnostics and mode validation. Windows also
exercises the actual timer's rearm/close/handle lifecycle without SDL, a window or
game execution. Linux tests the unchanged fallback. Tests do not validate gameplay
cadence or prove CPU savings; those require the user's A/B logs.
