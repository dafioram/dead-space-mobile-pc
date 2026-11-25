# Controls and options

Full keyboard, mouse and controller reference. Settings are saved in `userdata/port_settings.ini`
(created lazily when a setting, preset or binding is changed).

## Control presets

Two control presets are available in **Options -> Controls** and **Options -> Controller**
(**Control Preset**, saved as `control_preset = modern` or `classic`):

- **Modern** (default on fresh installs with no existing `userdata/port_settings.ini`):
  splits nearby interaction (`E` / `A` / `Cross`) from kinesis grab & release (`F` / `B` / `Circle`),
  adds a `Caps Lock` keyboard run toggle, lets stasis and zero-G jump trigger without holding aim,
  uses `RT`/`R2` for fire (aiming) & stomp (lowered) and `RB`/`R1` for melee (lowered) & alt-fire
  (aiming), cycles owned weapons on `D-pad Left / Right`, and supports PlayStation button labels
  and physical Touchpad click for the RIG on PS4/PS5.
- **Classic Port** (preserved automatically when an existing `userdata/port_settings.ini` has no
  `control_preset` key): keeps the original port bindings (`E` / `A` combined nearby interact +
  distant kinesis fallback, `F` stasis, `C` stomp, `Space` zero-G jump, `RT` fire/melee, `RB`
  stomp/alt-fire, `B` quick turn / aimed zero-G jump, D-pad direct weapon slots, `Back` RIG).

Existing saved/custom bindings in `userdata/port_settings.ini` are never reset on startup or
controller hotplug; switching **Control Preset** explicitly in Options applies the chosen preset's
defaults and clears old binding overrides.

## Playing (Modern preset)

In gameplay the mouse is captured; in menus it works as a touch pointer (moving it hides the keyboard
highlight). The arrows beside `//Options` at the top of the Options screen turn its pages.

| Input | Action |
|---|---|
| W A S D | move |
| Left Shift (hold) | run (subject to forward movement and not aiming) |
| Caps Lock | run toggle (stays latched while moving forward until movement stops or aiming starts) |
| Mouse | look / aim |
| Right mouse (hold) | raise weapon / aim; release to lower (automatically re-raises after a 180° quick turn or kinesis release if still held) |
| Left mouse | fire while aiming; melee when not aiming; launch a held kinesis object (takes priority regardless of aim) |
| Space | stomp with weapon lowered (**does nothing while aiming**) |
| V or Mouse 4 | dedicated melee, including while aiming; launch a held kinesis object instead while carrying |
| X or middle mouse | switch weapon firing mode (only when weapon is raised with aim held; does nothing when lowered or carrying) |
| E | nearby interaction only (doors, lockers, pickups, panels, benches, stores...); no kinesis fallback |
| F | kinesis grab / release (aiming or not): first press grabs, second press drops cleanly without a launch impulse |
| C | stasis (aiming or not) |
| R | reload |
| Q | quick turn (180°) |
| Left Ctrl | zero-G jump (aiming or not; still requires zero-G and a valid destination) |
| 1-4, mouse wheel | select weapon (mouse wheel skips unowned weapons and is a no-op with only one gun) |
| Tab | open / close the RIG (weapon select) |
| B | navigation line (locator) without opening the RIG |
| Esc | pause / back |
| W A S D / arrow keys (menus) | move the highlight between buttons; at the edge of an Options page, turn the page |
| Space / Enter (menus) | press the highlighted button |
| Space / Enter / click during a cinematic | skip it: the cinematic is ended at once through the game's own end-of-animation path (state 2, then 3), so control, HUD, objective and tutorial follow as at its natural end. If one is still playing half a second later, the game clock is fast-forwarded through it instead |
| hold Left Alt | free the cursor to click on screen (acts as touch) |
| F11 / Alt+Enter | toggle fullscreen (borderless) |
| F12 | screenshot (`shots/`) |
| Backtick / ~ | console (see [debug_menu.md](debug_menu.md)) |
| F9 | interaction report: writes what the interact key would use and every usable object nearby to `deadspace.log` |
| F10 | interaction debug view: hitbox (cyan), usable objects (white), active pick (green) |
| F5 | debug: painted vertex lighting normal / hidden / alone |
| F6 | debug view: light placement |
| F7 | debug view: hitboxes |
| F8 | switch renderer (legacy / native) |

### Classic Port keyboard differences

When **Control Preset** is set to **Classic Port**, the defaults that differ from Modern are:

| Action | Classic Port default |
|---|---|
| Interact | `E` (nearby interact, plus distant kinesis grab fallback and release when carrying) |
| Kinesis | unbound (`-`) |
| Run (toggle) | unbound (`-`) |
| Stasis | `F` |
| Stomp | `C` |
| Zero-G Jump | `Space` |

## Options menu and settings

The **Options menu** (title screen or pause menu) has six pages, named in its header; the last three
(**Graphics**, **Controls**, **Controller**) are the port's and scroll vertically (mouse wheel, drag,
or moving up / down past the last visible row with the keyboard; a bar at the right shows the position),
with smaller rows than the game's own pages. Turning a page with the keyboard highlights its first row.

- **General**, **Game**, **Camera**: the game's own options
- **Graphics**: **Anti-Aliasing** (MSAA Off/2x/4x/8x), **Renderer** (Legacy / Native), then, for the native renderer (see [Rendering](RENDERING.md)):
  **Texture Filtering** (Off, 2x-16x anisotropic), **Bloom**, **Sharpening**, **Ambient Occlusion**,
  **Dynamic Lights**, **Specular Highlights**, **Shadows** (Off/Low/Medium/High each),
  **Light Shadows** (No/Yes), **Light Contrast** (Off/Low/Medium/High) and **Vignette** (No/Yes); **V-Sync** (No/Yes, on by default; with it on, the
  monitor's refresh rate is the ceiling); **Frame Rate Limit** (60 / 120 / 144 / 240 / Unlimited,
  default 60; rates above 60 are untested beyond walking speed) and **Show FPS** (No/Yes, off by default)
- **Controls**: **Control Preset** (`Classic Port` / `Modern`), **Mouse Sensitivity** (bar, 10-300%),
  **Mouse Smoothing**, **Free Look (Standing)**, then one row per control: press its arrow, then the
  key or mouse button to use (`Esc` cancels). A key already used by another control swaps with it.
  **Reset Controls** (last row) restores the active preset's keyboard/mouse defaults.
- **Controller**: **Control Preset** (`Classic Port` / `Modern`), **Stick Sensitivity** (10-300%),
  one row per remappable controller action (`Start`/`Options`/`Menu` cannot be bound; actions may
  share a button when split by aiming), and **Reset Controller**.

MSAA choices depend on the driver. The menu and saved setting reflect the accepted sample count;
if a change cannot produce a complete framebuffer, the previous working target is retained.

`1-4` / mouse wheel (weapons), `Mouse 4` (melee) and middle mouse (alt fire) are fixed extras.

Mouse look is linear (no acceleration): at 100% each mouse count turns the camera 0.05 degrees.
Mouse smoothing averages the last 3 frames of movement (same total turn, less jitter). Free look:
standing still, the mouse orbits the camera around Isaac; moving, aiming or any action turns him
to face the camera. The game's own Look Sensitivity (Camera page) multiplies on top; at its default it
changes nothing.

Settings saved in `userdata/port_settings.ini`: `control_preset`, `msaa`, `mouse_sensitivity`,
`mouse_smoothing`, `free_look`, `renderer`, `anisotropy`, `reversed_depth`, `bloom`, `sharpen`,
`ambient_occlusion`, `dynamic_lights`, `specular`, `shadows`, `light_shadows`, `light_contrast`,
`vignette`, `vsync`, `show_fps`, `fps_limit`, `pad_sensitivity`, `bind_<control>`, `padbind_<action>`
(key names as SDL spells them, e.g. `Left Shift`, `CapsLock`; canonical controller button names such
as `A`, `RB`, `Touchpad` are stored in the INI and displayed using the active controller family's labels).

CLI options: `--msaa N`, `--width W --height H` (game resolution, default = desktop resolution), `--fullscreen` (borderless),
`--fps N` (frame cap for this run, 0 = off; overrides the Frame Rate Limit setting), `--verbose`, `--log FILE`.

Run `deadspace --help` (or `-h`) for all supported flags without starting the game.
`--assets DIR --lib FILE` use matching extracted game files elsewhere, `--data DIR` selects a different
userdata root (saves **and** settings) and `--no-save` plays on a disposable copy of it.
See [Debug controls and save protection](debug_menu.md) for examples, console commands, developer
tools and safety limits.

## Controller

Any controller SDL recognises works (plug in or swap at any time).

### Modern preset (Xbox and PlayStation)

| Action | Xbox | PlayStation (PS4 / PS5 / PS3) |
|---|---|---|
| Move / look | Left stick / Right stick | Left stick / Right stick |
| Aim (hold) | LT | L2 |
| Fire while aiming / stomp with weapon lowered | RT | R2 |
| Melee with weapon lowered / switch firing mode while aiming | RB | R1 |
| Launch held kinesis object | RT or RB (takes priority regardless of aim) | R2 or R1 (takes priority regardless of aim) |
| Nearby interaction only; break free in a struggle | A | Cross |
| Kinesis grab / release (aiming or not) | B | Circle |
| Reload | X | Square |
| Stasis (aiming or not) | Y | Triangle |
| Run (hold) | LB | L1 |
| Run toggle | L3 | L3 |
| Zero-G jump (aiming or not) | R3 | R3 |
| Locator (navigation line) | D-pad Up | D-pad Up |
| Quick turn (180°) | D-pad Down | D-pad Down |
| Previous / next owned weapon | D-pad Left / Right | D-pad Left / Right |
| RIG | View / Back | Touchpad click (PS4/PS5 when exposed); Share / Select fallback |
| Pause / back | Menu / Start | Options (PS4/PS5) / Start (PS3) |

### Classic Port controller differences

| Action | Classic Port default |
|---|---|
| Fire (aiming) / Melee (lowered) | RT / R2 |
| Alt fire (aiming) / Stomp (lowered) | RB / R1 |
| Interact + kinesis fallback | A / Cross |
| Quick turn (lowered) / Zero-G jump (aiming) | B / Circle |
| Direct weapon slots 1-4 | D-pad |
| Navigation line | R3 |
| RIG | View / Back / Share / Select |

### PlayStation support and capability fallback

- **Family detection:** DualShock 4, DualSense (PS5) and DualShock 3 controllers are identified via
  SDL controller type and display PlayStation button names (`Cross`, `Circle`, `Square`, `Triangle`,
  `L1`/`R1`, `L2`/`R2`, `Touchpad`, `Share`/`Options` on PS4/PS5, `Select`/`Start` on PS3) in both
  the Options menu and on-screen prompts.
- **Physical Touchpad click:** On PS4/PS5 controllers whose physical touchpad button is exposed by
  SDL (`SDL_CONTROLLER_BUTTON_TOUCHPAD`), the Modern preset defaults **RIG** to Touchpad click
  (never touch, swipe or finger movement).
- **Safe fallback:** If Steam Input, an adapter or a PS3/Xbox controller hides or lacks the touchpad
  button, **RIG** automatically falls back to `Back`/`View`/`Share`/`Select` so it is never
  inaccessible. Explicit custom `padbind_*` settings are preserved on connect/hotplug.
- **Light bar:** On a DualSense or DualShock 4 (any controller with an RGB light), the light shows
  the RIG's health colour: blue above half health, yellow down to a quarter, red below that and when
  dead; blue outside gameplay.

## Shared action semantics

- **Kinesis toggle:** In Modern, a fresh `F` / `B` / `Circle` press grabs an eligible kinesis target
  when not carrying one, and a fresh second press releases the held object cleanly via
  `GameObjectPlayable::abortTelekinesis()` without an added launch impulse or immediate re-grab.
  (In Classic Port, pressing `E` / `A` / `Cross` while carrying also releases the held object.)
- **Throw before fire/melee:** While carrying an object, `LMB` and `V`/`Mouse 4` (keyboard) and
  `RT`/`R2` and `RB`/`R1` (Modern controller) launch it instead of shooting, stomping, swinging or
  switching modes. One press never both throws and fires/swings; stale pending shots are cleared.
- **Aim hold across transitions:** Holding `RMB` / `LT` / `L2` through a 180° quick turn or kinesis
  release automatically re-raises the weapon once Isaac returns to normal locomotion.
- **Running:** Hold-run (`Shift` / `LB` / `L1`) OR run-toggle (`Caps Lock` / `L3`) requests running
  while moving forward and not aiming. Stopping movement, raising aim, opening menus, losing focus
  or disconnecting a controller clears the toggle.
- **Owned-weapon cycling:** Mouse wheel and Modern `D-pad Left / Right` share the same owned-gun
  traversal, skipping unowned slots, never selecting the stasis slot, and doing nothing if only one
  gun is owned.
- **Menus and context ownership:** In menus, the D-pad or left stick moves the highlight, `A`/`Cross`
  confirms, and `B`/`Circle` or `Start`/`Options` goes back. `Space`/`Enter` confirm menu items and
  skip cinematics rather than stomping.

## Prompts, menus, grabs and misc port behaviour

- **Dynamic prompts:** Touch instructions are reworded for keyboard/mouse and controllers (English
  only), and tutorial touch graphics (arrows, hand, device pictures) are hidden. Prompts, loading
  hints, open tutorial boxes and the grab prompt switch dynamically when you change between
  keyboard/mouse, Xbox, PS4/PS5, PS3 or **Classic Port** / **Modern** presets.
- **Power node locks:** Using one opens a small dialog. While it is up the mouse is free and the
  menu keys or the controller work on its buttons (first press highlights, `Space` / `Enter` / `A` / `Cross` confirms).
- **Quitting:** **Quit** is the last button in the main menu's bottom bar, after Play / Options / Info / Extras (details: [MAIN_MENU_QUIT.md](MAIN_MENU_QUIT.md)).
  Click it, or navigate to it with the menu controls and confirm. It opens the game's own "Are you sure you want to exit?" prompt, which `Esc` / `B` / `Circle` on the main menu also does; confirming closes the game. The window's close button and `Alt+F4` work at any time.
- **Bench and store:** While either is open the game is a menu, not gameplay: the camera does not move, the mouse is free, and the menu keys or the controller work on it (`W`/`A`/`S`/`D` or arrows / D-pad or left stick, `Space` / `Enter` / `A` / `Cross` to confirm, `Esc` / `B` / `Circle` to go back).
- **Grabs:** When a necromorph grabs the player, the Melee key, Interact key or Fire each advance
  the struggle, and a prompt near the bottom of the screen shows `PRESS [ ... ]` or blinking
  `TAP [ ... ]` (`A`/`Cross` on controller in Modern, Melee key on keyboard or Classic controller).
- **Fog banks and light shafts:** The game fades these effects out by distance over a band about two
  metres wide; the port smooths the opacity transition over ~1.4 s (`src/lights.cpp`, `smooth_fades`).
- **Idle locator:** The game's habit of showing the navigation line by itself when standing idle is
  patched out; the line only appears on its key/button.

## How interaction picks targets

A box in front of Isaac (1.8 m deep, 1.4 m wide, floor to above head height) points where the
camera looks. Every usable object has a zone: its 3D bounding box, padded to at least 0.5 m; objects
without a physical body (control panels, stores, power node locks, O2 stations) get a zone built
around the spot where the game shows their tap icon.

- In **Modern**, `E` / `A` / `Cross` uses only the nearest overlapping nearby-usable object (doors,
  pickups, lockers, terminals, benches, stores, covered switches) and never grabs kinesis objects;
  `F` / `B` / `Circle` targets only kinesis objects (grab within ~9 m straight ahead, or release when
  already carrying).
- In **Classic Port**, `E` / `A` / `Cross` uses the nearest overlapping nearby-usable object and,
  with nothing nearby in reach, falls back to grabbing a kinesis target within ~9 m straight ahead
  (or releasing if already carrying).

Plain shoot switches are not used with Interact (shoot them); covered ones are opened with Interact.
The nearby-object search measures distance to the interaction zone, not the object's origin, so
controls on large objects (such as a gondola) work at the object's edge even when the origin is far
away. The `F10` interaction overlay uses the same zone-based check (see [Debug views](debug_menu.md#debug-views)).

## See also

- [Main-menu Quit button](MAIN_MENU_QUIT.md)
- [One-press locator / navigation line](DIRECT_LOCATOR.md)
- [Movement cancelled when a menu takes input](MENU_MOVEMENT_HANDOFF.md)
- [Noninteractive cinematics and melee input](NONINTERACTIVE_CINEMATICS.md)
