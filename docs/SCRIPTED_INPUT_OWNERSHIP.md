# Scripted camera/input ownership

Scope: scripted control handoffs from upstream camera/input issue #11. This does
not change zero-G camera axes, collision, game scripts, animation timing or saves.
Gameplay confirmation is user-led; a successful build is not a confirmed fix for
every symptom in that report.

## Evidence and cause

The port sends player events directly rather than through the mobile HUD's input
route. Checking only pause, a recent `GameObjectCinematic` update and selected
store/bench states does not cover all native owners.

The supplied guest library's named functions establish these boundaries:

- `Hud::onEvent` uses its input state at `+0x2a4`. States 0/1 forward to the
  player scheme (including grapple controls); state 2 forwards to another object;
  state 3 forwards only when the passthrough byte at `+0x2a8` is set.
  `hudHideSoftly` sets that byte; `hudHide` clears it. These are behavioural
  descriptions, not recovered original enum names.
- `objectGetInput` claims state 2 and resets the embedded scheme at `Hud+0x10`.
  `objectReleaseInput` returns ownership only for the current owning object.
- `GameObjectPlayable::setFreezeFlag` stores its mask at `+0x1b4`. Bits 1/2
  freeze facing/position; `canChangeFacing` and `canChangePosition` test them.
  Both `ai::ActionIntroSequenceBrute` and `ai::ActionSpawnSequenceBrute` set all
  freeze bits in `start` and clear them in `onAnimEnd`. Those are not
  `GameObjectCinematic` updates: the prior cinematic-only check missed them.
- `enterBench`/`enterStore` select player state 19. `exitBenchOrStore` selects 21,
  `upgradeRig` selects 22 and `exitRigUpgrade` selects 24. Animation callbacks
  advance through 20/23; the exit callback returns 21 to ordinary state 0.
  Store state 6 calls `exitBenchOrStore` and releases HUD input before that exit
  animation ends. Native `canChangePosition` alone does not reject player state
  21, so simply checking it would not solve premature movement during exit.
- Native look events consult `canChangeFacing`. The port's standing free-look
  orbit and direct player catch-up yaw did not, so they could bypass a freeze.

Only these symbolic/layout observations are recorded here. The guest library,
disassembly fixtures and actual game assets remain private and are not packaged.

## Change

- Share live native HUD routing, full facing-and-position freeze, and the
  bench/store player-sequence checks between host capture and game-thread
  delivery. Partial freezes are not promoted to blanket input locks.
- During a locked player sequence after an object releases input, consume mouse
  gestures rather than forwarding them as mobile touch movement. Object-owned
  UI, pause/RIG/death menus and the ordinary Left Alt touch pointer still work.
- Recheck ownership for every queued event: interacting or opening the RIG can
  hand control away in the middle of a batch. Keep diagnostic jobs independent.
- Honour native `canChangeFacing` for look/orbit and catch-up yaw; clear the
  pending orbit when native ownership is lost. Keep the existing orientation
  math unchanged.
- Observe native `InputScheme::reset` without replacing its body. Increment the
  port event epoch, clear queued events/orbit, and invalidate any older local
  drain. The hook's displaced push/load instructions are not PC-relative.
- Synchronise main-thread mouse deltas, smoothing, deferred fire, aim/fire latches
  and sprint toggle with that reset, even if a complete handoff occurs between
  host frames. Keep the current physical movement keys/stick: held movement can
  resume after the animation, but blocked clicks are not replayed as shots.
- Refresh the HUD through `GameObject::getHud` on player updates, and retire its
  cached reference/epoch with the player on unload/reload.

This is not a general input-state rewrite. Existing cinematic skip, menu routing,
dialog debounce and gameplay actions remain. The current reset epoch is included
in ownership logs; `--trace-input` additionally logs native scheme resets.

## User playtest checklist

Use the combined `playtest` executable, replacing only the executable in your
existing setup. Keep a backup of your current executable. Do not use a public
release ZIP as a source for new game assets. No save editing is required; a
checkpoint before a relevant encounter is useful if one is already available.

1. **Brute introduction / scripted enemy reveal (highest priority).** First watch
   without input. Then retry while moving the mouse and holding a movement key
   or stick during the reveal. The scripted view must stay scripted, with no
   free orbit behind walls or player movement. Afterwards ordinary look/movement
   must return. Repeat with Free Look enabled and disabled if practical.
2. **Store entry/exit.** Enter from a standing free-look orbit; browse and exit
   without buying. Try movement/look during entry and exit. The UI must remain
   usable, the player must not slide out of the animation, and normal controls
   must return once it finishes. Repeat holding movement across exit, then
   releasing it: movement must stop normally.
3. **Suit purchase, if convenient in normal play.** During the suit-change and
   exit animations, try mouse/look, movement, aim and fire. Do not skip the
   animation just to test it. No sliding/free camera or delayed shot should
   appear; normal controls should return afterwards. Report separately if a
   suit upgrade is not available to test.
4. **Bench immediately after the store.** Open, navigate and close it; purchase
   an upgrade only if you want one. Afterwards WASD/mouse (or both sticks) must
   work without dragging the screen like touch controls. Also check a second
   store/bench visit. This is not a claim to fix the separate workbench crash.
5. **Short regressions.** Normal standing orbit -> walk/aim/melee; fire/reload;
   pause/RIG open/close; opening cinematic skip; death Continue and restored
   mouse look. If an enemy grabs you, check the usual struggle button still
   works. If already in zero-G, check walking, aiming and jumping still work;
   stationary wall/ceiling inversion is explicitly not fixed here.

For a failure, keep `deadspace.log` locally and report the scene, mouse/controller,
Free Look setting, which inputs were held, and whether controls returned. Do not
upload saves, APKs, assets or private diagnostic fixtures to the repository.

## Automated validation

Asset-free core tests cover native HUD routing (including hidden passthrough and
grapples), the complete 19..24 bench/store sequence, partial/full freezes, camera
permission, mid-drain takeover and reset-epoch rejection/restoration. Application
builds and all six CTest suites run on Windows and Linux. They do not exercise
the actual guest animations or prove the reported touch-control fallback gone.
