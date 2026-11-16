# Shared geometry diagnostics (user-operated test build)

This is an opt-in **observer**, not a streak fix. It runs before the Native/Legacy
draw split in `src/hle/gles.cpp`. It does not change submitted vertices, indices,
draw counts, transforms, game objects, cleanup, audio or save behavior. The normal
save/load reliability work is included independently on main. Interactive tests
belong to the user: the agent must not launch, drive or stop a game session.

## Quick test

1. Use your normal installation with the supplied test EXE, or supply its own
   original assets/library. The agent does not replace the installed EXE.
2. Prefer a copied userdata root selected with `--data`; add `--no-save` if you
   want disposable session progress instead of persistent test progress.
3. Open the console with **` / ~**, enter **`/geom on`**, then close the console.
   The bottom of the screen says `GEOMETRY ON`. Nothing is captured by default.
4. Let the scene run for a couple of seconds. With the streak visible, press
   **F4**. Keep the camera reasonably steady for about a second, until
   `REPORT READY` appears. If practical, mark once before combat and again just
   after a new streak appears. Do not deliberately replay a crash route.
5. **F8 still switches renderers**. If you compare Native/Legacy, mark each view
   separately and say which marker shows the problem. All normal combat, grapple,
   QTE, menu and controller input remains unchanged; only F4 is reserved while
   capture is explicitly armed.
6. Optional: enter **`/geom focus 1`**, close the console and look for the colored
   outline. Candidate numbers 1..12 refer to the most recent completed report.
   Yellow outlines the longest sampled triangle, magenta the thinnest. These are
   **x-ray overlays**, including occluded geometry: an outline crossing a wall is
   not itself evidence of the bug. Say which number, if any, actually coincides
   with the visible streak. `/geom focus 0` clears the outline. Normal draws are
   never hidden/replaced to identify a candidate.
7. Use **`/geom off`** to stop observation and free the rolling history. `/debug_off`
   also disables it. Queued reports continue draining into `deadspace.log`.
8. Send `deadspace.log` and, ideally, a short recording around the marker. Preserve
   the log **before another launch overwrites it**. Do not commit or put captures,
   recordings, screenshots or any extracted geometry in public packages.

`/geom status` reports armed state, frame/scene serial, retained history, pending
capture and candidate count. Spaces and underscores work, e.g. `/geom_focus_2`.
Invalid/overflowing candidate arguments are rejected. If no candidate overlaps
the streak, say so; it is not useful to guess that the highest score is the cause.

## What is recorded

- A rolling **120 native frames**, plus **30 after the marker**. At 60 FPS this is
  roughly two seconds before and half a second after; it is frame-based, not a
  guaranteed wall-clock window. Fast-forward/turbo consumes the window faster.
- Draw callsite/LR and its symbol, renderer, primitive/index type/count/range,
  array pointers, components/types/strides, buffer allocation serials, texture IDs,
  guest framebuffer, modelview/projection, viewport, blend/depth/cull/alpha/fog and
  ancillary state. Offscreen-target candidates have no game-screen outline.
- Bounded referenced-position/attribute reads; geometry-data fingerprints; local
  and **eye-space** bounds; longest and thinnest sampled triangles, their indices,
  local/eye/homogeneous clip coordinates, lengths/aspect and degeneracy counts.
- Up to **16 ranked candidates per frame**, then **12 distinct submission
  signatures** for detailed reporting. History logs the top two per retained
  frame; candidate reports describe their retained first/last observations and
  geometry-fingerprint changes. These times are **not object creation/destruction**.
  Ranked retention includes perspective **depth-disabled** effects as well as
  depth-tested draws and severe observations, rather than hiding a possible
  through-wall effect behind a depth-test filter.
- `[geometry] MARK`, `REPORT BEGIN/END`, candidate numbers and frame/scene IDs link
  the log to the user's recording. `REPORT READY` means candidates are available;
  log output drains in bounded batches rather than flushing hundreds of lines in
  every draw. Let the game run briefly before copying the log. Normal shutdown
  completes any pending report and drains its remaining output.

Positions support byte, unsigned byte, short, unsigned short, unsigned int, float
and 16.16 fixed decoding, with packed or positive interleaved strides. The
diagnostic understands triangles/strips/fans and indexed/non-indexed submissions;
other semantics are labeled unverified rather than assigned guessed widths.
Matrix state comes from the always-maintained GLES mirror, not expensive GL
queries. It describes original GLES state, not Native's later reversed-Z/shader
adjustments. Non-unit homogeneous vertices are not assigned affine edge metrics.

## Safety and honest limits

- Inspection is capped at **32,768 index/vertex references per draw**, **196,608
  per frame**, and **2,048 draw observations per frame**. The observer reports
  partial scans/budget exhaustion; large draws
  are sampled from the beginning, so a bad triangle outside that prefix can be
  missed. Frames retain ranked summaries, not every draw or raw buffers.
- Shadow-buffer extents and guest page mappings are checked before reads, with
  overflow-safe arithmetic and memcpy-based unaligned scalar decoding. Mapped
  guest pages **do not establish an allocation's size, ownership or liveness**.
  Guest allocation bounds are explicitly unknown. No stale source pointer is
  dereferenced when a delayed report/overlay is produced; those use copied values.
- At most eight distinct severe check warnings are emitted immediately per scene,
  to preserve invalid-span/non-finite observations before the original draw path
  might fail. Diagnostics **do not suppress or repair the draw/failure**. Large or
  thin triangles alone are never labeled corrupt; beams/cables can be legitimate.
- Memory, retained summaries, occurrence tracking, output queue and overlay line
  counts are bounded. Armed capture adds CPU work, and marking can cause a
  diagnostic pause. Timing-sensitive symptoms may behave differently. `/geom off`
  removes the observation overhead. This has not been gameplay/performance tested
  by the agent.
- A signature groups a callsite, source layout/pointers, buffer allocation serials,
  draw parameters, texture and repeated occurrence in a frame. Scratch arrays,
  address reuse and draw reordering can alias/change signatures. It is **not a
  stable mesh or object identity**. Owner/class/model/effect names remain unknown
  unless a later targeted, verified hook establishes them. No vtable is guessed
  or arbitrary guest memory searched to invent ownership.
- Scene changes finish pending reports early, reset history/focus and invalidate
  old candidates; capture remains armed for the new scene. Renderer switches do
  not erase history, and each observed summary records its renderer.
- No automatic screenshots, meshes, textures or separate diagnostic files are
  written. Numerical excerpts are private diagnostics inside the existing log.
  Only synthetic arrays/mutations are committed as tests.

## How to interpret results

First connect a candidate outline to the visible streak. Then inspect its raw
indices/strides, local geometry and transformed geometry. Bad index spans suggest
buffer/bridge issues; stretched submitted vertices point upstream toward CPU
deformation/effects; reasonable local vertices with a bad transform suggest a
transform problem. Reasonable geometry that persists unexpectedly may be an
effect-state/lifetime issue. Reasonable submissions with wrong pixels call for
render-state/depth/material analysis. None of those is established by ranking,
missing-effect warning timing or a clean scene-cache log alone.

The Native/Legacy shoulder-lighting difference is a separate report. Original
game behavior is unverified; this geometry observer is not a claimed armor fix.

## Automated validation

The asset-free `geometry_diagnostics` suite executes the production decoder,
capture lifecycle, copied-value reporting, clipping and focus policy with
synthetic data. Coverage includes position/index formats, strides/offsets,
transforms, primitive assembly, overflow/unmapped/truncated inputs, non-finite
values, enabled attribute ranges, scan/history/output bounds, marker overlap,
scene resets, unchanged source data, renderer-independent inspection and 30,000
generated mutations. Tests run without SDL/GL, a game library, game assets, JIT
execution or gameplay. Full application compilation validates the integration;
only the user can confirm actual streak/highlight behavior in the game.

Local Windows/Linux application builds pass all **15 CTest suites**; the
application-disabled Linux ASan/UBSan build passes all **14 suites**. Additional
warning-enabled syntax checks pass for the new observer/capture sources. These
are automated/synthetic checks, not gameplay or a claim that the wall-streak
cause has been identified. Original installations, assets and saves are not
replaced or modified by agent validation.
