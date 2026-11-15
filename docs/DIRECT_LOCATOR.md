# One-press locator / navigation line

Scope: issue #22, item 2. The default keyboard B binding and controller right-
stick click should activate the current-objective locator without another click.
Keep the native route, locator animation, restrictions, feedback and objectives;
do not replace the navigation line with a new waypoint or change game assets.

## Cause

The old shortcut queued two ordinary HUD commands: `BTN_RIG`, then `BTN_MARKER`.
The ownership gate correctly rejects gameplay after the first opens RIG, so it
also discarded the second command. Relaxing that gate for arbitrary queued HUD
events would let old gameplay inputs bypass menu ownership again.

## Native behavior and change

The supplied library's `Hud::canUseLocator` requires HUD state 1 and player input
route 0, then checks the player's locator permission. The native `BTN_MARKER`
handler calls `GameObjectPlayable::doLocatorMeshAnim(false)`. If it starts, native
code resets HUD messages and returns the HUD to state 0. The handler also keeps
the original objective popup, button feedback and failed-use behavior.

- Queue one `EV_LOCATOR`, checked by the existing ownership/epoch rules before
  it starts. Execute open-RIG/marker as one native-menu transaction, not two
  independent queued gameplay events. Ordinary movement/actions remain gated.
- After opening, recheck the same live player/HUD and exact RIG state/route,
  including dialog, noninteractive sequence and grapple/swarm/shake-off guards.
  Do not send the marker to another menu or a replacement/dead player.
- Native success closes RIG. On refusal, close only a RIG this shortcut opened
  and still owns, avoiding a stranded panel. If the user manually opened RIG,
  marker refusal leaves that panel alone. Never toggle a foreign/new UI owner.
- B (or its keyboard binding) can also activate the marker in an already-open
  RIG. The controller locator binding does likewise where it does not replace
  reserved menu confirm/back/navigation buttons. Tab/RIG and manual icon use
  retain their existing native handlers.
- Keep the movement cancellation from PR #24: entering the native RIG boundary
  still releases previously accepted walking. No new input exception, delayed
  retry, hard-coded player-state change or input replay after a menu is added.

## Validation

Asset-free core tests exercise the actual transaction helper: the old second
event is denied while the combined request reaches the marker, native success,
refusal to open, refusal to activate, restored versus manually opened panels,
repeated fresh requests, blocked/dead/foreign contexts and takeover during either
step. These are synthetic tests, not proof that the game drew a locator line.

User-led checks, preferably with `--no-save`:

1. Press B once during ordinary play with a valid objective. The native locator
   animation/line should appear, with no extra click or stranded RIG panel.
2. Repeat while walking/aiming, and with controller R3 and a rebound locator key.
   No walking latch should remain during the animation; normal controls resume.
3. Open RIG manually with Tab, then press B/R3. Also check the original marker
   icon, weapon selection, Tab close and controller menu confirm/back/navigation.
4. Try a busy/disabled/no-route context if naturally available. Native rejection
   must not leave a shortcut-opened RIG behind or bypass a gameplay restriction.
5. Pause, store/bench, a power-node dialog, a noninteractive movie and a natural
   grapple/swarm/shake-off must retain their controls, without a delayed locator
   firing when they finish.

Live locator display and interactive regression confirmation remain outstanding.
