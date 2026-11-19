# Experimental low-end attribution kit

User-operated only; not a release or an FPS optimization. Rendering quality,
cache policy, physics, audio queues and save policy are unchanged. Reports stay
private. No game assets or profiles are packaged.

## Launchers and comparisons

- `Run-Playthrough.cmd`: fixture detail every frame; guest/HLE detail 1/16 frames.
- `Run-Full-Attribution.cmd`: HLE every frame; use a short repro if sampling misses
  a hitch. More overhead; sampling can miss isolated hitches/alias periodic work.
- `Run-Attribution-Off.cmd`: NEW detail disabled, original stage/cache/audio and
  process diagnostics retained. Same EXE, fixes, settings and persistent profile.
- `Run-Geometry.cmd`: sampled detail plus optional geometry metadata.

All launchers share this kit's real local `userdata`, create fresh reports, and
must not run concurrently. Select matching existing data in Playthrough-Paths.json.
With all games closed, the user may COPY a whole profile into the new kit; never
move originals or mix isolated checkpoint/settings files. No profile is selected
or copied automatically. Do not use a link to an installed profile.

Compare short off/sampled/full runs at the same location/view/settings/activity,
in both orders. Different combat/routes or a capped FPS average are not a valid
overhead comparison. No automatic input, mode switching or rollback occurs.

## Timings

Original frame stages remain available. Detail is a second partition, NOT extra
time to add to those stages. Its nesting-aware clock separates guest/JIT Run from
SVC callbacks, including callbacks re-entering the guest. Run includes compilation
and JIT internals: NOT a guest instruction/function sampler. JIT instance setup is
separate. HLE names are 71 sanitized characters; 4096 IDs plus overflow, top 12
overall and top 8 non-GL per report. Name-based GL/JNI/sync/I/O groups are hints,
not proof of blocking. Background threads are excluded from main-thread detail.
At most 16384 detail scopes are timed per frame. scope_budget_skips exposes
saturation; skipped work remains charged to its enclosing bucket, so saturated
intervals are incomplete attribution rather than a clean fine-grained profile.
Sampled main CPU versus wall uses OS clocks; Windows CPU resolution is coarse.
Maxima carry max_loop (main-loop serial; ordinary hitch frame serial starts after
the startup baseline, so it is normally one less). Fixture detail also separates
glow/fan/beacon analysis, light setup and open-direction ray tests.

Fixture detail splits scan remainder, CPU texture lookup/copy, fallback GL readback,
mask generation, mesh sampling/local clustering, and global merging. Input workload
counts and worst scan mesh key are recorded. The 2.5ms between-mesh budget is
unchanged, not a limit on one mesh/readback. All timings include driver waits and
descheduling, not GPU execution. Sums are exclusive; max_inclusive_ns measures
the WHOLE nested invocation and cannot be summed across buckets.

bookkeeping_observed_ns measures only part of clock/counter work: a LOWER BOUND,
not total overhead. It excludes its own final clock read, branch/dispatch and cache
effects. sampler_elapsed_ms includes detail output, whose subsequent pause appears
in between-frame gaps. Tests include identical synthetic-work A/B measurements;
these are not gameplay overhead measurements. Real overhead needs the user-run
matched comparison. No timing is subtracted to make results look faster.

## Optional geometry

The follow-up recorder now covers shared GLES input on BOTH renderers, including
arrays/elements, strided numeric formats, matrices, primitive/blend/color metadata
and sampled local/eye/NDC bounds. Capture-Geometry.cmd enables it with newer HLE/
fixture detail OFF for a focused geometry test. Other launchers remain available.
Read [SHARED_GEOMETRY_CAPTURE.md](SHARED_GEOMETRY_CAPTURE.md) for current limits,
flag definitions, coverage counters and the distinction between input evidence
and final rendered pixels. This replaces the earlier Native-only coverage.
