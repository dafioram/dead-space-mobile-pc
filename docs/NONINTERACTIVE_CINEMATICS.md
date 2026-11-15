# Noninteractive scripted sequences must not receive melee input

Scope: discard V/melee, left-click/fire and their rebound/controller equivalents
during noninteractive movies and scripted player animations/camera handoffs.
Preserve grapple/swarm/shake-off inputs, ordinary fighting and existing movie skip.
No sound assets, damage, saves, cinematic animation/timing or cleanup are changed.
This is not a fix for the separately reported boss-scene teardown crash.

## Evidence

The supplied library's `GameObjectPlayable::doGenericAnim` selects player state
29. Both `GameObjectCinematic` and `GameObjectCinematic2` use that method. The
port previously watched only recent `GameObjectCinematic` updates and selected
bench/store/freeze states; generic-animation state 29 could still receive port
actions or fallback mouse-as-touch input.

The boss uses another path: `GameObjectBoss::stateTransitionCamera` calls
`GameObjectPlayable::doIdleFrozen`, which selects state 1. Returning to its player
camera calls `endIdleFrozen`, selecting ordinary state 0. This can leave native
HUD routing in the player route with no freeze bits set, so movie-only detection
misses it. The user's latest boss-intro log is consistent with that state.

Other known scripted entry points select tram-climbing state 27 and weapon-
collection state 30. Existing bench/store/suit transitions in states 19..24 and
full native facing-and-position freezes are locks too, but object-owned store/
bench UI remains interactive. Interactive states 9/10
are recognised by native `isGrappling(false)`; `doDementiaShakeOff` selects
interactive state 12. Unknown states and ordinary combat animations are not
guessed to be movies, nor is the whole boss fight or every non-player camera mode.

Native `Hud::objectGetInput` is called by store, bench, power-node lock prompts
and cinematics. Therefore a frozen player body must not disable an object's UI,
but object routing alone must not reopen a live movie or explicit tram/generic/
collection animation. Store/bench UI keeps its existing explicit exemption.

The native player action-event handler can play feedback audio for action 14
before calling `startSlash`, whose permission checks reject the action in these
locked states. Thus an audible slash does not, by itself, prove a slash animation/damage
or corruption occurred. Rejecting the input before that handler also removes the
unwanted sound, rather than muting sounds globally or altering combat checks.

## Boundaries

- A current cinematic update, known scripted state (1, 19..24, 27, 29, 30), or
  full native freeze denies gameplay capture and queued delivery. Partial freezes
  are not promoted to a blanket lock. Cinematic presence is timestamped
  immediately and expires if it stops updating.
- Native grapple routing, active grapple phases/attached swarms, native grapple/
  swarm states 9/10, and interactive shake-off state 12 explicitly exclude a
  sequence from the new classification. Object-owned UI also keeps its pointer
  despite frozen-idle/full-freeze body locks when no movie or explicit scripted
  animation owns input; bench/store UI retains its explicit exemption. Native pause,
  liveness, object-ownership and freeze checks still apply independently.
- The existing native player-event observation hook rejects melee action events
  before their body during these scenes, covering native/touch fallback as well
  as the direct port controls. It does not replace animation callbacks or filter
  ordinary combat by a broad `canSlash` rule.
- Raw mouse gestures are blocked during these scenes. A touch held before the
  sequence is cancelled rather than retaining a press/release across the handoff.
  Object-owned menus and pause/RIG/death UI keep their existing routing.
- Click/Space/Enter and controller cinematic skip remain, but a stale cinematic
  observation cannot steal a grapple input as a skip. The opening fly-by can
  still be skipped before there is a player object. A live tracked movie is also
  required for skip: boss camera/animation locks, tram/pickup sequences and
  Cinematic2 animations do not gain a new skip merely because input is blocked.

## Validation

Asset-free core tests cover cinematic/scripted-state/freeze/HUD-route/grapple
combinations, boss frozen-idle/return to combat, interactive shake-off, partial
locks, ordinary combat/aiming/zero-G/kinesis states, expiry and tick wrap,
mid-queue rejection, and fresh input after a native reset. They do not play a
movie, listen for sounds or perform a real grapple.

User-led checks, preferably with `--no-save`:

1. During a noninteractive movie or boss scripted camera sequence, press V,
   left click and the controller fire/melee binding: no player slash sound/action.
   Existing skippable movies may
   still skip when clicked; use V or a non-skipping control to watch uninterrupted.
2. Hold/click across the start/end: no delayed slash or shot on returning to play.
3. Check normal melee/fire immediately afterwards, plus pause/RIG/menu clicks.
4. During a natural enemy grab, swarm or interactive shake-off, check its usual
   inputs still work. Do not classify a scripted spawn/cheat encounter as gameplay
   confirmation.

Visual/audio/gameplay confirmation remains user-led. Keep logs and game evidence
private; no APKs, assets, saves or recordings belong in source control/releases.
