# MSAA framebuffer correctness test

This is a graphics-correctness fix, not a new performance experiment. Further
optimization work is on hold. It changes only presentation-target replacement,
MSAA fallback/accepted-setting reporting, and the selector's accepted value.

## Fix

The old target could keep a fog-coverage attachment at the previous sample count
while checking newly resized color/depth/normals. This produced framebuffer status
`0x8d56` (incomplete multisample), incorrectly disabled normals, skipped coverage
reallocation and could leave even the MSAA-off fallback incomplete.

The fix builds a complete proposal in fresh framebuffer/renderbuffer objects,
checks core, normals and fog coverage in order, and publishes only a complete
target. All multisampled attachments follow the driver's ACTUAL color sample
count. A rejected runtime proposal retains the previous complete framebuffer,
depth format, normal/fog availability and accepted sample count. It does not
silently reduce existing Native effects to accommodate a different MSAA setting.
Read/draw framebuffer and renderbuffer bindings are restored independently,
remapping bindings to retired presentation objects where necessary. Replaced or
failed proposal objects are deleted; allocation occurs on setting changes, not
each frame. Replacement temporarily holds both old and proposed resources.

Startup has no previous target. It tries bounded original-style depth/MSAA-off
fallbacks and refuses to continue if none produces a complete framebuffer. A
sample-count mismatch is not misreported as unsupported float depth. Startup and
menu preferences record the accepted count; the selector returns to it if the
driver rounds/refuses a request. The advertised global maximum is still only an
upper bound, not a guarantee of per-format support at every count.

## User-only gameplay checks

Use a new test folder and `--no-save` to preserve original settings/progress.
The prepared launcher starts at 1280x720, 4x MSAA and cap 60. Its existing
diagnostics are for correctness/log collection, not a new optimization benchmark.

1. In **Legacy**, cycle **4x → Off → 2x → 8x → 4x**, then back through them.
   Do this in the Graphics menu before loading a level and again during gameplay.
2. Repeat in **Native**, watching menus/HUD, fog, lighting, shadows and moving
   characters. Check that switching does not leave black output or missing effects.
3. Repeat the cycle a few times. Check normal input/audio and normal shutdown.
4. If a setting is unsupported, the selector may return to the previous value;
   that is deliberate safe rejection, not a successful change. Some drivers may
   round 2x to 4x. The accepted setting and image should remain truthful/usable.
5. Send the complete private `deadspace.log` and describe any failed transition,
   renderer and visible result. The log reports `MSAA target committed` or
   `MSAA change refused`; no system-wide CPU capture is needed.

Repeat on AMD/NVIDIA where available. These checks belong to the user: automated
tests do not execute the guest game, load game assets or drive gameplay.

## Automated coverage and limits

`presentation_target_tests` executes the actual production setup/transaction
helper with a synthetic driver: the old stale-coverage failure, repeated supported
transitions, complete-only publication, allocation and attachment failures,
unchanged old target/flags on rejection, initial optional-format fallback, actual
sample rounding/mismatch, split bindings, retirement/cleanup and no object growth
across repeated changes. Full regression and ASan/UBSan trees are run separately.

A private GL-only probe also uses the production host adapter and helper on the
AMD driver: off/2x/4x/8x cycles, normal/coverage attachments, color clear/resolve/
pixel readback, and a rejected beyond-limit allocation retaining the old complete
target. It creates only a hidden synthetic SDL/GL context, not a game process,
guest CPU/JIT, real profile or trace. This is not gameplay acceptance or NVIDIA
driver validation. No original files are repaired, overwritten or bundled.
