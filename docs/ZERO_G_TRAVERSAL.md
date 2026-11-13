# Zero-G slope-transition recovery

The floor ray beneath a grounded player can repeatedly snap the body onto the
lower ramp face while its leading collider contacts the next face. Local traces
showed continued movement input, correct alignment to the current floor normal,
and no sustained forward progress at the first two exterior airlocks.

`GameObject::snapToFloor` already supports a sphere sweep using its supplied
radius. The port briefly selects that existing mode after 100 ms of blocked
locomotion. A sweep lasts at most 400 ms, followed by 200 ms on the original ray
before another stall can arm recovery. Progress is measured against elapsed
simulation time and movement magnitude, including partial-stick movement.

Recovery is restricted to the living player in ordinary, grounded zero-G
locomotion. Stopping, pausing, leaving that state or changing player objects
clears it. Other objects, ordinary-gravity movement, jumping, initial floor
acquisition and death stay on their original paths. The game's floor-probe
distance, radius, collision filtering and orientation interpolation are unchanged.
No map coordinates, teleporting, collision disabling or asset changes are used
by the fix.

## Validation and limits

- Synthetic tests cover normal and partial-stick walking, short hitches, stall
  timing at several frame rates, bounded sweeps, cooldown, retries, cancellation,
  teleports and invalid input. They test the host policy, not guest collision.
- The bounded recovery crossed the first exit with sustained W, without jumping
  or teleporting, on an isolated checkpoint with full health afterward.
- An earlier continuous-sweep prototype crossed both exits. The bounded version
  still needs user playtesting at the second exit and in other zero-G areas.
- Backing down toward the first airlock can still snag. A matching approach in
  the pre-fix build also snagged; return-direction traversal is not claimed fixed.
- A successful zero-G jump was not independently reproduced. The hook excludes
  the jump state and airborne floor acquisition; this is not a claim of full
  gameplay regression coverage.
- Gameplay evidence uses the locally supplied fixed asset set, not an original
  APK. Saves, assets, logs and screenshots remain private and are not fixtures.

The user performs further gameplay tests; builds and asset-free tests remain
automated. The themed branch is `fix/zero-g-slope-transitions`, separate from
terminal-targeting changes.
