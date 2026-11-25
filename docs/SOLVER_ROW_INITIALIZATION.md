# Joint-row initialization correction

The supported ARM library's `btSequentialImpulseConstraintSolver` setup contains
a direct call at RVA `0x26325c` with `(row, 0, 136)`. Its actual target is
`EA::StdC::Memchr` at `0x28b4e4`: it searches bytes and returns a pointer or null,
but never writes the row. The return value is discarded. This is the shape of
an initialization call to `memset`, not a useful search.

Point-to-point constraint construction writes the diagonal linear Jacobian
components but relies on cleared off-diagonals and coefficients. Reused row
contents can therefore leak into the next solve. Private, user-operated
diagnostic captures found NaNs in joint-row inputs **before** solver iterations,
then NaN velocities and transforms. A separate finite runaway with stale CFM and
incorrect row directions reproduced the recorded solver impulses exactly in a
float32 numerical replay. Corrupt body transforms then propagated into skeleton
nodes, bone palettes and skinned vertex positions, producing stretched limbs or
large plane-like geometry. Stasis was present in some reproductions but is not
established as necessary.

## Production scope

`solver::install_joint_row_clear()` runs after library/HLE initialization and
before guest constructors. It checks both function identities and 15 instruction
words covering the argument setup, bad call, following builder and original
target. An unavailable/mismatched function, instruction or HLE stub leaves the
guest code unchanged and logs that the fix was not installed.

Only the call instruction is replaced with the SVC from the **existing registered
HLE `memset` handler**. Its normal r0/r1/r2 arguments already specify exactly 136
zeroed bytes. Only the SVC is inlined, not the stub's `BX LR`: dispatch resumes at
the following guest instruction. The original BL's LR/result/flag differences
are dead before use; LR is explicitly redefined at `0x2632c0`. No function-entry
hook, return interception, extra guest call, per-row recording or frame budget is
required. As with the original intended memset and the existing HLE handler,
the destination is the solver-owned row supplied by guest setup.

Ordinary row construction/solver iterations continue unchanged. This does not
globally modify `Memchr`, clear completed rows, clamp velocities, change stasis,
destroy limbs or suppress draws. It is independent of renderer/performance flags.
Main's existing invalid-limb containment and logging remain unchanged.

## Evidence and acceptance limits

A separate diagnostic candidate with the same zero-fill effect was tested in two
user-operated, roughly 90-second sessions in `02_mines_01`, Legacy rendering,
with no reported visible limb issue. It verified **66,642** row clears, with no
refusals/failures. All **788** sampled constructed joint rows had expected axis
directions and zero CFM, with no sampled NaN/runaway onset. These are initialization
operations/row witnesses, not counts of distinct limbs. Captures remain bounded;
they do not establish full campaign, every renderer or every physics regression.
That diagnostic candidate used an interior hook; the smaller inline-SVC
production adaptation still needs its own normal-build playtest.

The investigation branch and prior kits are preserved separately. No capture
hooks, diagnostic storage, F3 machinery or private reports are merged here.

For normal-build testing, confirm the startup message:
`solver row initialization: corrected joint-row Memchr call to memset (136 bytes)`.
Exercise the earlier reproduction and ordinary dismemberment, explosions,
kinesis and stasis; inspect main's cleanup logs. A cleanup event means some
invalid object state still occurred, not automatically that this exact mechanism
survived. Preserve its object/type/context so residual causes can be distinguished.
An absence of cleanup logs is not exhaustive correctness proof either.

## Separate friction-row follow-up

Static audit found a second direct caller at `0x26153c` in
`addFrictionConstraint(...).clone.0`. It passes `(row, -1, 136)` and immediately
discards the search result. This also looks like a fill-shaped call, but the
apparent fill value is **0xFF, not zero**. Friction construction explicitly
populates the arithmetic fields consumed by the current friction solve,
including its zero CFM, so an analogous gameplay failure has not been established.
The successful diagnostic candidate did not modify this site.

It is deliberately unchanged here. Follow-up should determine whether the
remaining untouched words are read in any relevant policy/path and validate
intended fill semantics before proposing a separate correction. Do not globally
replace `Memchr` or blindly apply this joint-row zero-fill there. Host/vendored
Memchr searches inspected in the audit use their results normally.

The earlier static lead/reference is retained in
[ANDROID_ASSET_SUPPORT.md](ANDROID_ASSET_SUPPORT.md#separate-physics-lead-not-part-of-apkhd-support);
APK/HD import behavior is not changed by this physics correction.
