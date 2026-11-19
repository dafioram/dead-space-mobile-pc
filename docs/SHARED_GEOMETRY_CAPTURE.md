# Shared GLES geometry capture (experimental)

This follow-up fixes a diagnostic coverage gap: the earlier Native-only recorder
could emit empty F3 dumps during Legacy gameplay. The new recorder observes both
`glDrawArrays` and `glDrawElements` BEFORE choosing Legacy versus Native. A Native
draw is recorded once, not again when forwarded internally. This is input evidence,
not a graphics/physics fix and not a promise to identify every visible artifact.

## Use

Run **Capture-Geometry.cmd**. It enables geometry recording and disables the newer
fixture/HLE detail layer to focus this experiment; ordinary frame/audio/ownership
diagnostics remain. Renderer, quality, resolution and saving rules are unchanged.
Keep the same renderer initially (Legacy is supported); press **F3** promptly when
the reddish line is visible. Note whether it is fixed in the world or follows the
camera/weapon, its first appearance/action, and any renderer switch. A privately
supplied screenshot can help match the log; no screenshots are taken automatically.
Quit normally yourself and return the whole report folder privately.

The kit uses its OWN persistent userdata. With every game closed, the user may
COPY an entire profile into it. Never move originals, mix isolated files or use
a userdata link to an installed profile. Launchers do not copy/select a profile.
The delivered folder can reference the explicitly known previous kit's matching
game data without copying or modifying it. No supplied assets are packaged.

## Evidence and limitations

- Per-report coverage counts explicitly distinguish Legacy/Native and arrays/
  elements. An empty marker warns that capture evidence is unavailable: it is
  never reported as a successful geometry validation.
- Metadata includes frame/scene/draw ID, guest return address, guest pointer or
  VBO ID/offset, original types/components/stride, texture ID, primitive mode,
  current and sampled vertex color, requested RGB/alpha blend factors, depth/
  alpha/cull/lighting flags, viewport, modelview and projection matrices.
  State flags: 1 blend, 2 depth test, 4 depth write, 8 alpha test, 16 culling,
  32 lighting, 64 matrix-index/weight array present, 128 texture unit 0, 256 fog.
  These are mirrored requested GLES states, not queried/verified GPU state.
- Sampled bounds are in **object/local, eye and NDC** spaces. Eye is not world
  space. NDC bounds include only successfully divided sampled points; clip-W
  range/nonpositive counts expose projection ambiguity. These are original GLES
  inputs, not Native's final reversed-Z/postprocessed output or GPU pixels.
- Texture pixels/material color are NOT read, so a reddish texture can have white
  vertex/current colors. The red-hint list is only an additional candidate list,
  not a blood/limb/effect classification or a filter on what gets recorded.
- Supported samples include points, lines, line strip/loop, triangles, triangle
  strip/fan; 8/16/32-bit indices; byte/short/int/float/fixed attributes; strided,
  unaligned, 2/3/4-component positions and normalized color attributes. Unsupported
  requests remain explicitly marked and original draws still proceed unchanged.
- Guest reads check mapping and address overflow; VBO reads check the existing
  CPU shadow. No pages are committed and no GL readbacks/queries/state setters
  occur in the recorder. A readable guest page is NOT proof the index belongs to
  the intended logical vertex allocation. No new bounds repair is applied.
- Samples are distributed across the draw, including late primitives, not just a
  prefix. Per-draw allowance adapts to the preceding frame's draw count and probes
  rotate between frames, so stable draw order does not always spend the budget
  on an early prefix. This is still partial sampling, not exhaustive coverage.
  At most 32 complete primitives (96 position reads) per draw, 8192 position
  attempts and 2048 analyzed submissions per main frame. Budget exhaustion and
  partial coverage are exposed. Unchecked vertices can still contain the defect.
- Retention is fixed: 256 recent submissions, 256 recent perspective submissions,
  32 ranked scene suspects and 16 recent red-hint submissions. One F3 dump emits
  at most 32 suspects + 64 perspective + 16 recent + 16 red-hint records, each with
  two bounded log lines, at most eight geometry dumps/process. Scene changes clear
  these histories. Suspects can predate a short recent ring; they are not exhaustive.

Flag bits: **1** nonfinite coordinate/calculation, **2** unreadable requested data,
**4** eye-space edge over 20 units, **8** unsupported/invalid input format,
**16** near-zero homogeneous W, **32** long thin triangle (edge >2, aspect >1000),
**64** partial/budgeted coverage. Long edges and thin triangles may be legitimate
beams, walls, decals or effects. Zero flags do not certify geometry correct.
No draw is dropped, modified, clamped or hidden based on these flags.

Geometry time and report time are measured in the logs; markers themselves can
pause rendering. Geometry time includes input-view setup, analysis and retention,
but excludes the initial opt-in check and final clock-read cost: not total overhead.
With identical settings/scene, Run-Attribution-Off.cmd provides
a no-geometry comparison; no automatic gameplay or mode switching occurs. Synthetic
checks measure overhead on a deterministic CPU workload, NOT real-game overhead.
The agent only runs asset-free tests/early CLI paths; the user operates gameplay.
