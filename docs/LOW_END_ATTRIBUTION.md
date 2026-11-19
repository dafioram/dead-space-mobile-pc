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

OFF by default. With Run-Geometry.cmd press F3 promptly after a streak and note
the enemy/explosion/action and persistence. Native draws (including dynamic bodies)
record guest source address, mode/count/index range, model-to-world transform,
and sampled local/world bounds AFTER guest vertex generation. No pre-skinning
skeleton is captured, so these cannot prove deformation is responsible.

Limits: 64 positions/draw, 4096 positions/frame, up to 32 triangles/draw while budget
remains; 256 recent records and 32 scene suspects; newest 32 plus up to 32 suspects
per marker, eight geometry dumps/process. Later F3 still retains ordinary frame
history. Scene changes clear draw history. Suspects may predate the recent ring.
Only metadata is retained: no raw mesh/texture payload, screenshot or recording.

Flags: 1 nonfinite sampled coordinate/result; 2 sampled index outside built range;
4 world edge over 20m. Long edges can be legitimate. Zero flags do NOT certify
unchecked geometry. Zero checked vertices means no valid sampled bounds. No draw
is clamped, suppressed, repaired or hidden.
