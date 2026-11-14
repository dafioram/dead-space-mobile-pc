# Continued damage after a zero-G grapple: investigation

Status: user-reported; not yet independently reproduced or fixed. Kept on
`diagnostics/zero-g-grapple`, separate from the zero-G camera rotation fix.

A bounded run of the combined build on a disposable copy of the retained first
spacewalk checkpoint reached the exterior with the trace enabled. The sampled
player remained at 200 HP, grapple phase -1 and no attacker reference; the route
did not produce an active enemy/grapple encounter. This verifies trace-enabled
startup and ordinary traversal only, not successful grapple release or absence
of the reported bug. The assistant-owned instance was stopped afterwards. No
forced spawns, teleporting, invulnerability or health changes were used, and the
original checkpoint, assets and user's installation were not modified.

The report is that after visibly pushing a monster away, the player could move
but continued taking damage. Visual separation alone does not establish whether
the native grapple ended, the attacker resumed an attack, another enemy hit the
player, or oxygen caused the health loss.

## Native evidence

- The port's struggle input uses native `emulateGrappleGestureEvent` for swipe
  phases 1/7 and native `onGrappleEvent` for tap phase 5. It does not directly
  clear a grapple, detach the attacker or change health.
- `GameObjectPlayable::updateGrapple` generates ongoing damage through
  `HitEvent::initGrapple` in active grapple phases. The inactive phase is -1 at
  player `+0x37c`; the retained attacker shared pointer starts at `+0x384`.
- `endGrapple` writes phase -1, clears freeze flags, restores normal control/HUD
  on a successful release and resets the attacker reference. A persistent
  grapple hypothesis requires observing whether that function ran and whether
  phase/reference actually retired, not forcibly clearing them whenever the
  monster looks detached.
- `HitEvent::initGrapple` records hit type 9 at `+0x54`, source at `+0`, and
  requested damage at `+0x64`. Requested damage is not actual HP loss: native
  damage processing may scale or reject it.

## Opt-in observation

Start the diagnostic-capable playtest build with `--trace-grapple`. Normal
launches do not install the extra diagnostic hooks. The option logs:

- native grapple transitions and `endGrapple` calls, including their callers;
- player damage requests, hit type, source, source/player distance and HP before
  processing;
- sampled state/phase/attacker and actual HP changes on subsequent player
  updates, plus position and surface up.

The hooks observe only. They never suppress hits, grant invulnerability, alter
health, detach objects or call `endGrapple`. The hook entry instructions were
checked for safe displacement; the existing checked hook installer is retained.
Trace logging can affect timing, so any reproduced fault should also be checked
with tracing disabled.

## What would distinguish the causes

1. Type-9 damage continues with an active phase after the visible push-off:
   inspect the remaining grapple phase, native end callback and attacker AI.
2. Type-9 damage continues after phase -1/reference retirement: investigate the
   sender/call chain rather than hiding all post-grapple damage.
3. Other hit types after release: identify the source and distance; it may be a
   separate attack instead of a stale grapple.
4. HP falls without matching damage requests: examine oxygen or direct health
   changes. Do not label it grapple damage from the health bar alone.

A short recording showing the monster, push-off, subsequent movement and health
bar, together with the corresponding local trace, is useful if this remains
unreproduced. Mention the room, surface (floor/wall/ceiling), input used to escape
and whether Free Look was enabled. Keep saves, recordings, game libraries and
diagnostic logs private; none belong in public packages or commits.
