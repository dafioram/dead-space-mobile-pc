# Noninteractive cinematics must not receive melee input

Scope: discard V/melee, left-click/fire and their rebound/controller equivalents
during noninteractive movies and scripted generic player animation. Preserve
grapple/swarm/QTE controls, ordinary fighting and the existing cinematic skip.
No sound assets, damage, saves, cinematic animation/timing or cleanup are changed.
This is not a fix for the separately reported boss-scene teardown crash.

## Evidence

The supplied library's `GameObjectPlayable::doGenericAnim` selects player state
29. Both `GameObjectCinematic` and `GameObjectCinematic2` use that method. The
port previously watched only recent `GameObjectCinematic` updates and selected
bench/store/freeze states; generic-animation state 29 could still receive port
actions or fallback mouse-as-touch input.

The native player action-event handler can play feedback audio for action 14
before calling `startSlash`, whose permission checks reject the action in this
state. Thus an audible slash does not, by itself, prove a slash animation/damage
or corruption occurred. Rejecting the input before that handler also removes the
unwanted sound, rather than muting sounds globally or altering combat checks.

## Boundaries

- A current cinematic update or generic-animation state 29 denies gameplay
  capture and queued delivery. Cinematic presence is timestamped immediately,
  including before the next host frame, and expires if it stops updating.
- Native grapple routing, active grapple phases and attached swarms explicitly
  exclude a sequence from that new noninteractive classification. Existing native
  pause, liveness, object-ownership and freeze checks still apply independently.
- The existing native player-event observation hook rejects melee action events
  before their body during these scenes, covering native/touch fallback as well
  as the direct port controls. It does not replace animation callbacks or filter
  ordinary combat by a broad `canSlash` rule.
- Raw mouse gestures are blocked during these scenes. A touch held before the
  scene is cancelled rather than retaining a press/release across the handoff.
  Object-owned menus and pause/RIG/death UI keep their existing routing.
- Click/Space/Enter and controller cinematic skip remain, but a stale cinematic
  observation cannot steal a grapple input as a skip. The opening fly-by can
  still be skipped before there is a player object. No new skip is introduced
  for Cinematic2 sequences that did not already support it.

## Validation

Asset-free core tests cover all cinematic/generic-animation/HUD-route/grapple
combinations, ordinary combat/aiming/zero-G/kinesis states, expiry and tick wrap,
mid-queue rejection, and fresh input after a native reset. They do not play a
movie, listen for sounds or perform a real grapple.

User-led checks, preferably with `--no-save`:

1. During a noninteractive movie, press V, left click and the controller fire/
   melee binding: no player slash sound/action. Existing skippable movies may
   still skip when clicked; use V or a non-skipping control to watch uninterrupted.
2. Hold/click across the start/end: no delayed slash or shot on returning to play.
3. Check normal melee/fire immediately afterwards, plus pause/RIG/menu clicks.
4. During a natural enemy grab or swarm, check the usual struggle inputs still
   work. Do not classify a scripted spawn/cheat encounter as gameplay confirmation.

Visual/audio/gameplay confirmation remains user-led. Keep logs and game evidence
private; no APKs, assets, saves or recordings belong in source control/releases.
