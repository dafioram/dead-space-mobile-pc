# Resource setters, restoration and read-only enemy inspection

Independent `feat/debug-console-tools` branch, based on production main through
`590b7d2`. Saving-reliability changes, scene-cache work and enemy killing are not
part of this change. No grant runs automatically at startup or profile load.

## Commands and boundaries

See [debug_menu.md](debug_menu.md) for exact syntax. `/credits`, `/nodes` and
`/rich` **set** native balances instead of adding resources. The preset is
500,000 credits and 200 nodes. A single-resource setter leaves the other field
alone; custom `/rich` requires both amounts. Zero is valid. Values must fit
0..2,147,483,647, avoiding signed native UI/count overflow without claiming a
retail cap. Inputs are rejected rather than silently clamped.

The wallet writes use the same inventory fields as the existing developer
`rich` command. The native player->inventory and credit/node accesses are checked
against the supported library's instructions before writes. This is not an item
pickup or a purchase: it does not register credit spending/node-find achievements
or change suit/weapon ownership. The older file-based developer command retains
its existing syntax/defaults; it is not the new slash-command preset.

`/restore` calls native maximum-health/maximum-stasis getters, then native
setHealth (immediate) / setStasis and triggerO2Refill. Invalid maxima or missing
native functions refuse the operation. Oxygen refill retains native permissions,
sounds and timing; inability/no need to start it is reported, not relabeled as
full restoration. Death is not reversed and scripts/grapples are not cancelled.

New commands capture the current player/world and native input-reset epoch,
execute during the next player update and reject stale contexts even if object
addresses are reused. Mutations additionally use existing native gameplay
ownership/liveness policy and exclude grapples/swarms. Diagnostic jobs do not
gain permission to write through a menu or cinematic. Rejected jobs are not
replayed after closing a menu or reloading a level. These commands do not change
the behavior of older developer jobs.

`/enemies` identifies enemies via the native virtual isNecromorph predicate and
uses each subclass's virtual isAlive query, including bosses. It obtains fresh
world references per command, computes true 3-D distance and sorts by distance
with an address tie-breaker. The summary counts matches/alive matches; at most
the nearest 10 rows are printed. Dead and hidden actors remain distinguishable.
No uniform HP field is invented. Bodyless base-position fallbacks and invalid
positions are not counted as objects at world origin. It performs no damage,
death, spawn, remove, AI or inventory operations.

These are opt-in cheats/diagnostics, not save-protection features. Mutations can
persist via normal autosaving; use `--no-save`. That flag must keep temporary
progress/settings behavior intact. No original assets, saves, screenshots,
recordings, logs or dump fixtures belong in this branch or its packages.

## Validation and user checks

The asset-free `console_debug` suite tests recognized/unrecognized commands,
strict argument counts/numeric bounds, malformed/overflow inputs, set-to
idempotence, zero/lower amounts, preservation of the other balance, native-limit
sanity, stale/reused contexts, mutation versus read-only policy, and 3-D radius
boundaries/non-finite/extreme values. Tests do not execute retail code or prove
native behavior. The existing help test requires every command to be documented.

Windows/Linux application builds and all 10 CTest suites pass. A bounded Windows
`--no-save` check loaded the supplied Android-origin chapter-2 checkpoint without
a developer level jump. Console commands verified the preset twice, individual
setters preserving the other balance, negative-amount/zero-radius rejection,
and refusal of resource/restore mutations from an open RIG. After closing RIG,
the refused credit change had not been replayed. A damaged player's health rose
from approximately 50 to the native suit maximum of 200; native maximum stasis
was 1.0. Normal-atmosphere oxygen correctly reported no eligible refill. The
read-only listing found three nearby native Slashers. The private process shut
down normally; all five source-profile file hashes remained unchanged.

This does not confirm refill while oxygen is depleting, every suit/module,
boss/corpse inspection, long lists, controller use or all script/grapple
boundaries. Those remain user-led checks. No save/load reliability or scene-cache
fix is implied by this smoke test.

Subsequent user confirmation: `/restore` refills oxygen to full as desired.
That confirms the user's tested oxygen case, not every suit/module or native
eligibility state. All further interactive playtesting is reserved for the user;
the agent should build/run synthetic checks without launching or driving gameplay.

Live checks should use a disposable profile:

1. Set credits and nodes individually, checking RIG/store/bench values. Run
   `/rich` twice and confirm 500,000/200 rather than accumulation. Try zero and
   malformed arguments. Do not buy anything solely to test a command.
2. With a damaged living player, run `/restore`; check suit-specific health and
   stasis maxima. Check oxygen while actively depleting, during a refill and in
   normal atmosphere. Confirm no revival or forced script/grapple exit.
3. Try new mutations from a store/RIG and during a scripted/interactive takeover;
   require refusal, not delayed application after control returns. Issue a new
   command after returning to gameplay to confirm ordinary operation.
4. Compare `/enemies` near known enemies, hidden/dormant actors, corpses and a
   boss if convenient. Check empty areas, fractional radii and the 10-row bound.
   Ensure inspection causes no attacks/damage beyond normal game progression.
5. Keep natural gameplay/visual confirmation separate from synthetic tests or
   developer-positioned/spawned smoke checks. Verify the original profile's
   hashes remain unchanged in `--no-save` runs.
