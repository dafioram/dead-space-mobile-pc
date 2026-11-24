# Controls and options

Full keyboard, mouse and controller reference. Settings are saved in `userdata/port_settings.ini`.

The existing controls below are the **current implementation**. The
[planned modern layout](#planned-modern-layout-not-implemented) records the agreed
rework separately; it is **documentation only**, not available in the game yet.

## Playing

In gameplay the mouse is captured; in menus it works as a touch pointer (moving it hides the keyboard
highlight). The arrows beside //Options at the top of the Options screen turn its pages.

| Input | Action |
|---|---|
| W A S D | move (Shift = run) |
| Mouse | look / aim |
| Right mouse (hold) | raise weapon / aim |
| Left mouse | fire while aiming (hold right mouse); melee when not aiming; launch a kinesis object |
| R | reload |
| F | stasis |
| E | use what is in front of you (doors, lockers, pickups, panels, benches, stores...); kinesis on an object further along your line of sight |
| Q | quick turn (180°) |
| V or Mouse 4 | melee (plasma saw swing) |
| C | stomp |
| X or middle mouse | switch fire mode (alt fire) |
| Space | zero-G jump |
| 1-4, mouse wheel | select weapon |
| Tab | open / close the RIG (weapon select) |
| B | navigation line (locator) without opening the RIG |
| Esc | pause / back |
| W A S D / arrow keys (menus) | move the highlight between buttons; at the edge of an Options page, turn the page |
| Space / Enter (menus) | press the highlighted button |
| Space / Enter / click during a cinematic | skip it: the cinematic is ended at once through the game's own end-of-animation path (state 2, then 3), so control, HUD, objective and tutorial follow as at its natural end. If one is still playing half a second later, the game clock is fast-forwarded through it instead |
| hold Left Alt | free the cursor to click on screen (acts as touch) |
| F11 / Alt+Enter | toggle fullscreen (borderless) |
| F12 | screenshot (shots/) |
| Backtick / ~ | console (see [debug_menu.md](debug_menu.md)) |
| F9 | interaction report: writes what E would use and every usable object nearby to deadspace.log |
| F10 | interaction debug view: hitbox (cyan), usable objects (white), what E would use (green) |
| F5 | debug: painted vertex lighting normal / hidden / alone |
| F6 | debug view: light placement |
| F7 | debug view: hitboxes |
| F8 | switch renderer (legacy / native) |

These are the default bindings. The **Options menu** (title screen or pause menu) has six pages,
named in its header; the last three (Graphics, Controls, Controller) are the port's and scroll vertically (mouse wheel, drag, or just
moving up / down past the last visible row with the keyboard; a bar at the right shows the position),
with smaller rows than the game's own pages. Turning a page with the keyboard highlights its first row.
- **General**, **Game**, **Camera**: the game's own options
- **Graphics**: **Anti-Aliasing** (MSAA Off/2x/4x/8x), **Renderer** (Legacy / Native), then, for the native renderer (see [Rendering](RENDERING.md)):
  **Texture Filtering** (Off, 2x-16x anisotropic), **Bloom**, **Sharpening**, **Ambient Occlusion**,
  **Dynamic Lights**, **Specular Highlights**, **Shadows** (Off/Low/Medium/High each),
  **Light Shadows** (No/Yes), **Light Contrast** (Off/Low/Medium/High) and **Vignette** (No/Yes); **V-Sync** (No/Yes, on by default; with it on, the
  monitor's refresh rate is the ceiling); **Frame Rate Limit** (60 / 120 / 144 / 240 / Unlimited,
  default 60; rates above 60 are untested beyond walking speed) and **Show FPS** (No/Yes, off by default)
- **Controls**: **Mouse Sensitivity** (bar, 10-300%), **Mouse Smoothing**, **Free Look (Standing)**,
  then one row per control: press its arrow, then the key or mouse button to use (Esc cancels). A key
  already used by another control swaps with it. **Reset Controls** (last row) restores the defaults.

MSAA choices depend on the driver. The menu and saved setting reflect the accepted sample count;
if a change cannot produce a complete framebuffer, the previous working target is retained.

1-4 / mouse wheel (weapons), Mouse 4 (melee) and middle mouse (alt fire) are fixed extras.

Mouse look is linear (no acceleration): at 100% each mouse count turns the camera 0.05 degrees.
Mouse smoothing averages the last 3 frames of movement (same total turn, less jitter). Free look:
standing still, the mouse orbits the camera around Isaac; moving, aiming or any action turns him
to face the camera. The game's own Look Sensitivity (Camera page) multiplies on top; at its default it
changes nothing.

All of these are saved in `userdata/port_settings.ini` (`msaa`, `mouse_sensitivity`,
`mouse_smoothing`, `free_look`, `renderer`, `anisotropy`, `reversed_depth`, `bloom`, `sharpen`, `ambient_occlusion`, `dynamic_lights`, `specular`, `shadows`, `light_shadows`, `light_contrast`, `vignette`, `vsync`, `show_fps`, `fps_limit`, `bind_<control>`; key names as SDL spells them, e.g. `Left Shift`).

Options: `--msaa N`, `--width W --height H` (game resolution, default = desktop resolution), `--fullscreen` (borderless),
`--fps N` (frame cap for this run, 0 = off; overrides the Frame Rate Limit setting), `--verbose`, `--log FILE`.

Run `deadspace --help` (or `-h`) for all supported flags without starting the game.
`--assets DIR --lib FILE` use matching extracted game files elsewhere, `--data DIR` selects a different
userdata root (saves **and** settings) and `--no-save` plays on a disposable copy of it.
See [Debug controls and save protection](debug_menu.md) for examples, console commands, developer
tools and safety limits.

Touch instructions are reworded for keyboard and mouse (English only): tutorial boxes and loading
hints name the keys bound when the game starts, and the tutorials' touch graphics (arrows, hand, device
pictures) are removed from the layouts, leaving the text box. On the loading screen that waits for
input ("Press any key to continue"), any key or mouse button continues and the left / right arrow keys
page through the hints.

### Controller
Any controller SDL recognises works (plug in at any time). Default layout, after the Dead Space remake:

| Input | Action |
|---|---|
| Left stick / Right stick | move / look and aim |
| LB (hold) or L3 (click) | run; the click keeps running until the stick is let go or the weapon is raised |
| LT / RT | aim / fire (melee swing while not aiming) |
| RB | alt fire while aiming, stomp otherwise |
| A | interact, kinesis; break free of a grab |
| X / Y | reload / stasis |
| B | quick turn; zero-G jump while aiming |
| D-pad | the four weapons |
| R3 | navigation line |
| Back / Start | RIG / pause |

In menus the D-pad or left stick moves the highlight, A confirms, B or Start goes back. The **Controller**
page of the Options menu has the camera stick sensitivity (10-300%) and one row per button (press the
row's arrow, then the button; Start cannot be bound; two actions may share a button when one applies
only while aiming). Settings: `pad_sensitivity`, `padbind_<action>`.

Light bar: on a DualSense or DualShock 4 (any controller with an RGB light) the light shows the RIG's
health colour: blue above half health, yellow down to a quarter, red below that and when dead; blue
outside gameplay.

Prompts follow the device last used: tutorial boxes, loading hints and the grab prompt name controller
buttons after any controller input and keys again after any key press, mouse click or mouse movement
(an open tutorial box is rewritten on the spot). Button names are Xbox style on every controller.

Power node locks: using one opens a small dialog. While it is up the mouse is free and the menu keys
or the controller work on its buttons (first press highlights, Space / Enter / A confirms).

Quitting: **Quit** is the last button in the main menu's bottom bar, after Play / Options / Info / Extras (details: [MAIN_MENU_QUIT.md](MAIN_MENU_QUIT.md)).
Click it, or navigate to it with the menu controls and confirm.
It opens the game's own "Are you sure you want to exit?" prompt, which Esc / B on the main
menu also does; confirming closes the game. The window's close button and Alt+F4 work at any time.

Bench and store: while either is open the game is a menu, not gameplay: the camera does not move, the
mouse is free, and the menu keys or the controller work on it (W/A/S/D or arrows / D-pad or left stick,
Space / Enter / A to confirm, Esc / B to go back). The first press highlights the top-left control.
Moving onto an entry of the store's list or the bench's weapon row selects it (its details show) and the
list scrolls to follow; confirm on the selected entry is the same as Purchase / Upgrade. On the bench's
upgrade grid the highlight moves from node to node, confirm puts a power node in, and Purchase applies.

Grabs: when a necromorph grabs the player the game asks for touch gestures (swipe along an arrow, tap
repeatedly; swarms are tapped off one by one). With the port, the Melee key, the Interact key or Fire
each make whatever move the struggle is waiting for, and a prompt near the bottom of the screen shows
`PRESS [ key ]` or a blinking `TAP [ key ]` (it names the Melee key). The game's own "Swipe" / "Tap"
labels read "Press <key>" / "Tap <key>" (English).

Fog banks and light shafts: the game fades these effects out by distance as the player walks into them,
over a band about two metres wide, which at a run looks like an instant cut. The port spreads the change
over time (about 1.4 s from fully shown to gone, and the same coming back): each frame, after the game
has set an effect's opacity and before it is drawn, the opacity actually used moves toward the game's
value at a limited speed (src/lights.cpp, `smooth_fades`).

The game's habit of showing the navigation line by itself when the player stands idle (which also
tilts the camera down) is patched out; the line only appears on its key.

## How E picks

A box in front of Isaac (1.8 m deep, 1.4 m wide, floor to above head height) points where the
camera looks. Every usable object has a zone: its 3D bounding box, padded to at least 0.5 m; objects
without a physical body (control panels, stores, power node locks, O2 stations) get a zone built
around the spot where the game shows their tap icon. E uses the one object whose zone overlaps the
box nearest your line of sight, and nothing else. With nothing in reach, E grabs a kinesis target
within ~9 m straight ahead. Plain shoot switches are not used with E (shoot them); covered ones are opened with E.

The nearby-object search measures distance to the interaction zone, not the object's origin.
Controls on large objects (such as a gondola) can therefore be used at the object's edge even when
its origin is far away. The F10 interaction overlay uses the same zone-based distance check
(see [Debug views](debug_menu.md#debug-views)).

## Planned modern layout (not implemented)

This section is the design target for an optional **Modern / Remake-style**
preset, not a description of shipped bindings. Keep **Classic Port** and existing
saved/custom bindings intact; adopting the new preset must be explicit, not a
silent reset of `userdata/port_settings.ini`. No gameplay code, default settings,
binding migration, menu entries or controller detection have been changed for
this proposal.

### Mouse and keyboard

These are ordinary gameplay actions; held-object priority is defined below.

| Input | Planned action |
|---|---|
| W A S D | Move |
| Mouse movement | Look / aim; retain existing sensitivity, smoothing and free-look options |
| RMB (hold) | Raise weapon / aim; release to lower |
| LMB | Fire while aiming; melee when not aiming; throw takes priority while holding a kinesis object |
| Space | Stomp only with weapon lowered; **does nothing while aiming**, not alternate fire |
| V or Mouse 4 | Dedicated melee, including while aiming; throw instead while holding a kinesis object |
| X or middle mouse | Switch the weapon's native firing mode only when raised with RMB; no action with weapon lowered or while carrying a kinesis object |
| E | Nearby interaction only: doors, pickups, lockers, terminals, benches, stores, etc.; **no kinesis fallback** |
| F | Kinesis grab / release, with or without aiming |
| C | Stasis, with or without aiming |
| R | Reload |
| Q | Quick turn (180 degrees) |
| Left Ctrl | Zero-G jump, with or without aiming; not RMB + Ctrl |
| Left Shift (hold) | Run, subject to normal movement/state restrictions |
| Optional keyboard run-toggle binding | Controller-style run toggle; Caps Lock was suggested, but its default is not finalized |
| Mouse wheel up / down | Previous / next owned gun |
| 1-4 | Direct gun selection, alongside cycling |
| B | Locator without opening the RIG |
| Tab | Open / close RIG |
| Esc | Pause / back |

LMB deliberately retains un-aimed melee rather than copying the controller
trigger's un-aimed stomp. Space provides the separate keyboard stomp binding.
Mode switching uses the existing per-weapon action, not a new universal mode or
an added automatic shot.

### Xbox and PlayStation

| Action | Xbox | PlayStation |
|---|---|---|
| Move / look | Left stick / right stick | Left stick / right stick |
| Aim (hold) | LT | L2 |
| Fire while aiming / stomp with weapon lowered | RT | R2 |
| Melee with weapon lowered / switch firing mode while aiming | RB | R1 |
| Nearby interaction only; break free in a struggle | A | Cross |
| Reload | X | Square |
| Stasis, aiming or not | Y | Triangle |
| Kinesis grab / release, aiming or not | B | Circle |
| Run (hold) | LB | L1 |
| Run toggle | Left-stick click | L3 |
| Zero-G jump, aiming or not | Right-stick click | R3 |
| Locator | D-pad Up | D-pad Up |
| Quick turn (180 degrees) | D-pad Down | D-pad Down |
| Previous / next owned gun | D-pad Left / Right | D-pad Left / Right |
| RIG | View / Back | Touchpad click on PS4/PS5 when exposed; Select on PS3 |
| Pause / back | Menu / Start | Options / Start |

Touchpad means its **physical click**, not touch, swipe or finger movement.
Choose defaults using both controller family and exposed button capabilities.
If Steam Input/an adapter presents a PlayStation controller as Xbox or hides its
touchpad, use an available Back/Share/Create equivalent or user-rebound control;
never leave RIG inaccessible. Preserve explicit custom bindings rather than
overwriting them on connection. Labels/prompts must reflect the actual controller
family and effective mapping, rather than the current Xbox-only names.

Controller stomp still requires lowering the weapon: RT/R2 fires while aiming.
Space while aiming is a no-op, so it does not bypass that restriction on keyboard.

### Shared action semantics and priority

- **Kinesis toggle:** a fresh F / B / Circle press grabs an eligible target when
  not carrying one. A fresh second press releases the current hold without an
  added launch impulse. Release must not immediately grab another target. It is
  a release, not teleporting the object, freezing it or forcing its velocity to
  zero. Existing gravity, collisions and game restrictions remain authoritative.
- **Throw before fire/melee:** while carrying an object, LMB and the dedicated
  melee inputs V/Mouse 4 throw it instead of firing or swinging. On controller,
  RT/R2 and RB/R1 throw while carrying, regardless of the aim modifier; the
  held-object action takes priority over their normal fire/stomp/melee/mode split.
  One press must not both throw and then shoot, stomp or swing. X/middle mouse
  remain mode-switch-only inputs and do nothing while carrying an object.
- **No repeat toggling:** key repeat or a held button must not repeatedly
  grab/release, change mode, quick-turn or cycle weapons. A new physical press
  is required for the next discrete action. Holding a fire input retains the
  weapon's normal firing behavior; stale pending fire must not cause a second
  action after a throw or release.
- **State checks:** stasis, kinesis and zero-G jump do not require the aim
  modifier, but are not forced through invalid native states. Zero-G jump still
  requires zero gravity, an eligible state and a valid destination. No ordinary
  jump, free-flight, manual-heal mechanic or physics-policy change is introduced.
  Other combinations, including stomp while carrying, remain subject to native
  eligibility and need explicit regression checks, not an invented throw binding.
- **Running:** hold-run OR run-toggle requests running. Releasing LB/L1/Shift
  does not change the toggle; toggling off while hold-run is down does not stop
  the hold request. Centering/stopping movement or aiming clears the toggle.
  Running still requires forward movement and an eligible state. Existing menu,
  focus, scripted-input and device-disconnect resets must not leave it latched.
- **Cycling:** read the game's current gun and owned gun slots; skip unowned
  entries and wrap in the existing slot order. With only one owned gun, do
  nothing instead of reselecting it. Never select the stasis-module slot. Respect
  the game's weapon-change checks. No duplicate inventory-tracking array is needed.
- **Context ownership:** these mappings apply to gameplay only. Menus keep
  D-pad/stick navigation, A/Cross confirmation and B/Circle back. Space/Enter
  remain menu confirmation/cinematic skip, not stomp there. RIG toggle/close,
  pause, console, struggles/swarms, death screens and scripted sequences retain
  their established ownership and must not leak a gameplay action. Utility/debug
  shortcuts are unchanged by this proposal.

### Implementation work still required (not started)

This is more than editing default key/button values:

1. **Separate interaction from kinesis.** `interact::use()` currently combines
   nearby-use selection and a distant kinesis fallback, and both activate an
   object with the same use event. Give the modern preset distinct interaction
   and kinesis routes: E/A/Cross must not grab, and F/B/Circle must not operate
   a nearby door/terminal instead. Reuse the existing target eligibility,
   range/cone and nearby-use ranking where appropriate; splitting selection
   alone may not be enough if the chosen object's use handler initiates kinesis.
   Preserve Classic Port's combined route and existing ammo/door/limb interaction
   priority. Update F9/F10 descriptions/previews to match the selected action.
2. **Expose and validate non-throw release.** The port has no dedicated release
   binding, but the guest does have `GameObjectPlayable::abortTelekinesis()` and
   a separate beam-abort path, distinct from `fireTelekinesis()`. Therefore a new
   release physics system should not be assumed necessary. Verify those paths
   actually detach supported targets without a launch impulse and restore beam,
   player and object state correctly. Read live player/target/transition state on
   the game thread; do not treat a stale host flag or an acquisition transition
   as proof of a safely releasable held object.
3. **Centralize contextual dispatch.** Add the grab/release action, controller
   melee and weapon-cycle actions as needed; apply held-object priority before
   aiming-dependent routing. Enforce Space's aimed no-op and X/middle mouse's
   raised-weapon-only behavior. Remove the controller's current aim requirement
   for zero-G jump and quick-turn filtering where it conflicts with the new
   dedicated mapping. Retain game-side eligibility checks and one-press rules.
4. **Share existing mechanisms, not duplicate state.** Factor the mouse-wheel
   owned-gun traversal for controller cycling and add the single-gun no-op.
   Keep the existing LB/L1 + L3 run behavior; a keyboard run toggle, if chosen,
   needs its own exposed binding and correct keyboard stop/aim/reset handling.
5. **Bindings, persistence, capabilities and prompts.** Add an explicit preset
   choice/new actions without resetting saved/custom controls. Existing one-key
   rebinding and fixed mouse extras need to represent the intended alternatives
   and contextual sharing safely. Add touchpad-click naming/binding and capability
   fallback, controller-family labels, hotplug behavior, menu rows and accurate
   tutorial/struggle prompts. The current controller prompt code treats melee as
   the fire button, which will no longer be generally correct.
6. **Regression tests and user-operated acceptance.** Cover raised/lowered weapon,
   acquiring/holding/throwing/releasing, rapid presses/held inputs, no target and
   target removal; nearby usable objects versus kinesis targets; one/multiple
   guns; native refused actions; saved bindings/preset switching; PS3/PS4/PS5 and
   Xbox-style/masked devices; menu/RIG/console/focus/hotplug transitions and
   struggles. Verify release physics and no duplicate attack on a gameplay test.
   These tests, implementation and builds have not been started on this branch.

## See also

- [Main-menu Quit button](MAIN_MENU_QUIT.md)
- [One-press locator / navigation line](DIRECT_LOCATOR.md)
- [Movement cancelled when a menu takes input](MENU_MOVEMENT_HANDOFF.md)
- [Noninteractive cinematics and melee input](NONINTERACTIVE_CINEMATICS.md)
