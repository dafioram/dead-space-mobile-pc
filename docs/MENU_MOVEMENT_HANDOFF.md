# Cancel delivered movement when a menu takes input

Scope: issue #22, item 3 (walking into RIG or a power-node confirmation keeps
Vandal moving). Do not change game scripts, physics velocity, animation timing,
save data, menu actions or the independent renderer/audio/locator reports.

## Cause and native boundary

The port delivers movement as a Vector2Event (type 1006, subtype 1) directly to
the player. The native handler retains the directional input until another move
event replaces it. Releasing host capture previously queued an ordinary zero
move, then cleared the host's moving flag. After a menu/dialog took ownership,
the game-thread ownership/epoch gate discarded that stop along with new input.
Clearing the queue or the host flag alone does not release an accepted command.

The reported clip is consistent with this: the power-node confirmation stays
visible while Vandal backs away. This is source-level diagnosis, not an automated
reproduction of the playtester's exact input timing.

The supplied library's `GameObjectPlayable::onEvent` checks
`canChangePosition` before accepting subtype 1, and returns consumed on accepted
movement. A zero vector with run flags cleared uses that same native input path;
it does not clear rigid-body velocity. `Hud::isInRig` recognises native HUD state
1. `Hud::objectGetInput` stores the owner, selects route 2 and resets its scheme.
The two new entry observations preserve their original bodies; their displaced
instructions are ordinary push/load or move/store instructions, not PC-relative.

## Change and limits

- Track the current player's last **accepted port movement**, separately from
  physical key/stick state and the ordinary event queue. Queued/rejected movement
  is not recorded as delivered. A native/mobile/script movement event retires
  the port's claim to the previous command.
- Cancel once before native RIG entry or object input takeover, covering native
  mouse/controller paths as well as keyboard bindings. Use native reset and the
  player-update ownership boundaries as fallbacks, including mid-batch handoffs.
- Cleanup is allowed despite menu/object ownership, but never grants permission
  for new movement, look or actions. It still requires the current live player,
  native movement permission, no grapple routing/phases/swarm and no interactive
  grapple/swarm/shake-off player state. When native death/animation/freeze owns
  motion, retire the old claim rather than forcing or postponing a stop into it.
- Use a separate guest event buffer because the handoff can occur inside an
  action whose original event is still in use. Retire the latch before invoking
  native code to make nested observations safe. Failed native acceptance is not
  reported as a successful stop.
- On host capture loss, invalidate queued commands/epochs instead of enqueueing
  another stop that can be rejected. Diagnostic jobs remain independent. Current
  physical keys/stick are retained: a released control stays stopped, whereas an
  intentionally held control may resume once gameplay regains input.
- Retire delivery state on player destruction/replacement, including address
  reuse. Do not write player movement fields, physics velocity or freeze flags,
  and do not pause the world or disable normal menu/grapple controls.

`--trace-input` adds handoff messages with reason, player/state/route,
eligibility and whether the native zero event was consumed. Normal per-event
trace can show the zero Vector2Event before RIG/object takeover. Keep logs private.

## Validation

Asset-free core tests cover backward/forward/strafe/diagonal movement and sprint,
blocked/stale queues versus independent cleanup, one-shot release, ordinary
zero moves, rejected delivery/stop, native movement takeover, eligibility across
states/routes/grab/liveness/native permission, re-entrant observations and player
destruction/replacement/address reuse. These are synthetic policy/lifecycle tests,
not a live game or grapple confirmation.

User-led checks, preferably with `--no-save`:

1. Walk or sprint, open RIG, then release movement while RIG is open. Vandal
   should stop at entry and remain stopped when it closes. Test forward/backward
   and strafe, keyboard and controller, including mouse/controller RIG entry.
2. Hold movement through RIG close: movement should resume only after control
   returns. Repeat rapid open/close and repeated visits; no old movement bursts.
3. Backpedal immediately before opening a power-node prompt. While it is open,
   Vandal must not continue walking. Test cancel and confirm, and both releasing
   movement and deliberately holding it until the prompt closes.
4. Check store/bench entry and exit, pause/resume, Left Alt pointer release and
   window focus loss. Menus must still accept input; no delayed shot/action.
5. Check ordinary walking/sprinting, zero-G travel, a natural grapple/swarm/
   shake-off, a scripted reveal and death/reload. Native motion and required
   gestures must remain usable. Do not call a synthetic/cheated encounter live
   gameplay confirmation.

The user confirmed the reported movement fix works, including the bench and a
sewer door unlocked with a power node: these interactions no longer retained
walking. This Windows test loaded an Android-origin chapter-2 save, then used a
console level jump to reach the sewer. It is menu-handoff confirmation, not a
natural chapter-transition or exhaustive grapple/swarm/scripted-motion check.
Those broader interactive regressions remain outstanding.
