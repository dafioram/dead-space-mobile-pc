# Controls and options

Full keyboard, mouse and controller reference. Settings are saved in `userdata/port_settings.ini`.

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

## See also

- [Main-menu Quit button](MAIN_MENU_QUIT.md)
- [One-press locator / navigation line](DIRECT_LOCATOR.md)
- [Movement cancelled when a menu takes input](MENU_MOVEMENT_HANDOFF.md)
- [Noninteractive cinematics and melee input](NONINTERACTIVE_CINEMATICS.md)
