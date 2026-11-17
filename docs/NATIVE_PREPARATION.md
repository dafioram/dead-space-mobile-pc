# Native CPU vertex and normal preparation

The Native renderer now defaults to optimized CPU vertex conversion and indexed
triangle normal preparation. It dispatches vertex formats/component counts per
array, initializes complete output records in one pass, and precomputes normal
offsets in the existing per-draw scratch storage.

`--native-prep optimized` selects these calculations (the default).
`--native-prep reference` keeps the original calculations for diagnosis/comparison.
Neither flag selects a renderer: choose **Native** in Graphics for both runs.
Legacy does not use these routines. The startup log records the requested mode.
The choice is startup-only and does not modify the stored graphics settings.

## Preservation

- Current-draw inputs are rebuilt on every use; no new cross-frame/pointer-only
  geometry cache, culling, GPU upload policy or retained container is added.
- Unaligned data, formats/component counts, array offsets/defaults, unsigned-byte
  color scaling, fixed conversion and unknown-format float fallback are retained.
- Float arrays retain multiplication by identity to preserve the original
  signalling-NaN behavior. Do not replace it with plain copies or fast-math.
- Normal welding retains exact position bits, hashing/probing, triangle/corner
  accumulation order and invalid-index behavior. The same scratch capacity is
  reused; scratch contents are rewritten per draw in either mode.
- No changes to renderer quality, lighting, MSAA/framebuffer handling, geometry
  visibility, JIT, dispatch, synchronization, frame pacing, audio, gameplay or saves.

## Validation and measured scope

Asset-free `vertex_prep_tests` compare complete output records bit-for-bit across
formats, strides/misalignment, components, defaults, offsets, float edge cases,
seams, winding/degenerates, invalid/trailing indices, maximum indexed span,
changing inputs and scratch reuse. `vertex_prep_tests --benchmark` optionally
measures synthetic routine cost; timing is not a pass/fail assertion or gameplay
speedup claim. These tests do not execute a game, GL context or JIT.

User-led tests on two PCs reported no incorrect visual effects. On the newer PC
both modes remained near the 60 FPS cap, without an established whole-game CPU
saving. On the older PC a same-checkpoint Native playthrough pair showed roughly
5-7% higher FPS depending on the comparison window, with lower frame-work time.
Memory was lower in optimized recordings on both PCs, but differing resource
histories prevent claiming a guaranteed RAM reduction. These are limited ordered
playthrough pairs, not deterministic replay or universal performance guarantees.

For a comparison, use the same Native settings/checkpoint and a fixed viewpoint
after warmup, followed by comparable combat/moving meshes/effects. Repeat in the
opposite order where practical. `--no-save` permits temporary settings/progress
without writing the source profile. Keep private assets, saves, logs and captures
out of source control. Separate diagnostic/playtest branches may add `--perf`;
this main-targeted change does not import that independent diagnostics system.
