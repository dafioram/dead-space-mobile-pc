# Scene-cache lifetime boundary

Independent `fix/scene-cache-lifetime` branch, based on production main through
`590b7d2`. Save/load reliability and debug-console features remain separate.

## What is corrected

The host light/fade code previously retained raw scene-node addresses and reset
its caches only when a numerically different GameWorld address appeared. Native
destruction and same-address allocation reuse were not retirement events. A
cached fade write could therefore target memory no longer owned by that node.
This is an independently identifiable lifetime weakness, not a proven explanation
for every reported scene-animation or exit crash.

- Native GameWorld construction starts a new generation; destruction retires the
  world's host observations **before** native objects/model references release.
  Reusing the same address cannot preserve the previous world's cache.
- Native Node construction invalidates any previous observation immediately.
  The new node becomes observable at the common base-constructor completion
  site, after its parent/fade fields initialize. Node generations never revive
  an older handle, even when the replacement has the same vtable.
- Retirement covers Node, Group, Mesh, SkinnedMesh, Camera, M3G World and
  Mesh-derived ParticleEffect destruction. Wrapper deleting destructors that
  call a hooked complete destructor are covered by that call; the separately
  implemented Group/Node/Camera/World variants are hooked individually.
- Registered flare/animated/fade roots carry both world and node tokens.
  Smoothing requires live tokens, a supported native type and mapped field
  ranges. Group children are freshly read from a validated child vector and
  must still name that group as their parent. Parent walks are bounded and
  reject cycles. Mapping/type checks alone never create a lifetime.
- Scene, fade-state, fog-buffer hints, fixture snapshots and fade timing retire
  with their owner. Fog hints also carry node identity; unrelated constructors
  do not erase live hints. Fixture extraction copies vertex positions and only
  publishes results if the source lifetimes still match.
- Native transform queries use balanced, temporary native references to their
  node/ancestor chain, never permanent cache-owned references. Native transform
  and release calls run without the registry mutex, avoiding lock inversion with
  native cleanup; only the verified lock-free native retain leaf runs inside it.
  This does not redesign the engine's native threading/ref-count protocol.

The supported native library's 25 hook sites are checked by symbol address and
instruction pairs before installation, including non-PC-relative sites after
PC-relative prologues. The native retain leaf is checked in full. Unsupported
libraries get explicit error logs and no unverified cached-node accesses; they
must be investigated, not relabeled as a successful fix.

## Preserved behavior and diagnostics

Live-node opacity still follows the existing native targets with the same
1.4-second smoothing, enable threshold, child bounds and fixture/light settings.
ParticleEffect nodes remain supported. No geometry, brightness, culling, shader,
save format, native child animation, reference release or allocator operation is
disabled or skipped to hide a defect. This is not a fix for the reported dark/
see-through chapter-6 store instance.

`[scene-cache]` log lines identify world construction/retirement and generation,
with bounded model/Object3D/IndexBuffer teardown headers. Malformed headers have
an independent warning budget so normal teardown samples cannot consume it.
Diagnostics read header values only, never dereference a suspect animation-track
or storage pointer and never alter/skip native cleanup. String storage uses its
native two-byte layout, including the shared empty string; it is not checked as
a four-byte pointer vector. Existing light tracing additionally samples qualified
fade writes. Private logs/dumps/assets/recordings do not belong in source control
or portable packages.

## Validation and remaining user playtest

The asset-free `scene_cache` suite runs the actual production host handlers with
synthetic memory/native-call stand-ins. It covers stale and same-address world/
node reuse, delayed old-world teardown, pending construction, normal/particle
fades, foreign/retired children, invalid vtables and vector headers, unmapped/
wrapping spans, fixture/fog invalidation, cyclic parents, balanced temporary
references, retirement during a transform, mutex availability during native
transform/release and read-only diagnostics including valid empty-string headers.
These are not retail execution or visual/teardown confirmation.

Windows and Linux application builds pass all 10 suites on this independent
branch. A clean Linux application-disabled build passes all 9 asset-free suites;
the production-adapter test also passes AddressSanitizer/UndefinedBehaviorSanitizer.
The combined playtest additionally retains the separate `console_debug` suite.

All 25 hook locations and seven Node-family retain slots were also inspected
read-only against the supported local library. No game process was launched,
stopped or driven for this change. Interactive playtesting is the user's task.

Suggested user checks with the combined playtest and `--no-save`:

1. Repeat the chapter-5/store -> chapter-6 developer-load route, and separately
   start chapter 6 directly. Check normal fades/lighting rather than only crashes.
2. Repeat the chapter-6 route through the two space walks, bench/store visits,
   then pause-menu Quit -> confirmation. Check natural transitions/reloads too.
3. Observe the particular dark/see-through store in both renderers separately;
   do not interpret a clean exit as proof that this visual issue is corrected.
4. If another crash occurs, preserve the exact executable identity, log and dump
   before launching again, because startup overwrites the installation log.

Neither a build/test pass nor cache retirement proves the reported animation/
allocator crashes are solved. Native scene ownership/corruption remains a
separate investigation if the failures persist.
