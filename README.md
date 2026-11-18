# Dead Space (2011 mobile) — native PC port

An unofficial, fan-made port that runs EA / IronMonkey's 2011 Android game *Dead Space* natively on
Windows and Linux (x86-64), without an Android emulator. The game's own 32-bit ARM code (`libDeadSpace.so`) runs
on the dynarmic JIT; everything it expects from Android (libc, pthreads, OpenGL ES 1.1, JNI/Java, audio,
assets) is reimplemented on the host with SDL2 and desktop OpenGL.

On top of the original game the port adds keyboard + mouse and controller play, an optional new renderer
(dynamic lights, baked shadow maps, ambient occlusion, bloom, anti-aliasing), higher frame rates, texture
packs, a Quit button, skippable cutscenes and an extended Options menu.

**No game data is included.** You need your own copy of the Android game as an APK file. This project is
not affiliated with or endorsed by Electronic Arts; Dead Space is a trademark of Electronic Arts Inc.

This README describes current `main`. Downloaded releases contain the code from their
tagged commit, which may be older; check the release notes and included `BUILD_INFO.txt`.

**Quick links:** [Windows install](#installing-windows) · [Linux install](#installing-linux) ·
[Saves and backups](#saves-and-backups) ·
[Controls](#playing) · [Controller](#controller) · [Rendering](#rendering) ·
[Troubleshooting](#troubleshooting) · [Building](#building)

## Installing (Windows)

1. Download the Windows installer, `DeadSpaceMobilePCPort-TAG-Setup.exe`, from the
   [Releases](../../releases) page and run it.
2. Choose where to install (the default is a folder in your user profile; no administrator rights are
   needed) and pick **your Dead Space APK file** when asked. The installer copies the game's data out of
   the APK into the install folder; this takes a moment. Allow about 400 MB for the installed
   game, plus extra free space for staged extraction and any existing imported data.
3. Start the game from the Start menu or the desktop shortcut.

Windows may warn that the installer is from an unknown publisher: it is not code-signed.

- **Updating**: run a newer installer and choose the same folder. Leave the APK field empty to keep the
  game data that is already there. Back up the complete `userdata` folder first, with the game closed;
  the installer keeps saves and settings. Different existing game data is not overwritten by import.
- **Uninstalling**: Settings > Apps > Installed apps > *Dead Space Mobile PC Port*, or run
  `uninstall.exe` in the install folder. It asks whether to delete your saves too; texture packs you
  added are left alone.
- **Which APK**: the inventoried original-layout ARM32 APKs A–D passed user-led gameplay checks.
  They share the same stock assets; A/C/D (D: Remastered) share the baseline game core, while B
  has a recognized CPU-detection library variant. These checks do not establish compatibility
  with every APK or every existing save.
  Names alone do not establish compatibility. The legacy `assets/guest_libs` layout remains
  importable with its existing converted-texture policy. **Reanimated/HD is detected but not imported
  yet**; it fails before extraction, without changing existing game data.
  See [APK support and isolated testing](docs/APK_IMPORT.md) and the
  [exact-copy inventory](docs/ANDROID_ASSET_SUPPORT.md).
- **Where things are** (all inside the install folder): `userdata/` saves and `port_settings.ini`,
  `gamedata/` the imported game files, `mods/textures/` texture packs, `deadspace.log` the last run's log.

### Without the installer

Download and extract the Windows portable ZIP (`deadspace-TAG-windows-x86_64-portable.zip`)
into a new writable folder, keeping its documentation and dependency notices.
`deadspace.exe` is statically linked and needs no separate SDL2 or MinGW runtime DLLs.
Place your APK next to it (it is imported on first start), or run in PowerShell:

```powershell
.\deadspace.exe --apk "D:\Games\A.apk"  # first run: imports into gamedata/
.\deadspace.exe                        # afterwards
```

For separate APK tests, extract without starting the game, then launch with a separate profile:

```powershell
.\deadspace.exe --apk "D:\Games\my-game.apk" --game-dir "D:\DSM-tests\A\gamedata" --import-only
.\deadspace.exe --game-dir "D:\DSM-tests\A\gamedata" --data "D:\DSM-tests\A\userdata"
```

Use a fresh destination for each different APK. Imports never replace different existing data;
identical data can be reused. `--data` selects saves/settings, **not** extracted assets.
The optional `tools/prepare_apk_test.py` helper creates isolated EXE/data/user-launcher folders
without running gameplay. These private folders contain game assets: never distribute them.

### Requirements

Windows 10 or 11, 64-bit; a graphics card and driver with OpenGL 3.0 or newer for the Native renderer
(Legacy is available in Options > Graphics). Use a driver providing desktop OpenGL compatibility support;
some Native enhancements require additional capabilities. Allow about 400 MB for the installed game,
plus space for your APK, staged imports, saves and any texture packs. A controller is optional: any pad SDL
recognises works, with the Dead Space remake's layout by default.

## Installing (Linux)

Linux x86-64 portable builds are available on the [Releases](../../releases) page;
you do not need Wine or the Windows installer.

1. Download `deadspace-TAG-linux-x86_64-portable.zip` and extract it into a writable folder.
2. Install the host SDL2, desktop OpenGL, zlib and C++ runtime libraries. For example, on
   Ubuntu/Debian:

   ```sh
   sudo apt install libsdl2-2.0-0 libgl1 zlib1g libstdc++6
   ```

3. Open a terminal in the extracted folder and run:

   ```sh
   chmod +x deadspace
   ./deadspace --apk "$HOME/Games/A.apk"  # first run: supply your own APK
   ./deadspace                         # subsequent runs
   ```

The Linux ZIP is not an AppImage or a fully static binary; it needs compatible host libraries
and a working desktop OpenGL driver. If your distribution's runtime is too old, see
[Building](#linux-x86-64) to build against your system's libraries.
Controls and game-data/profile options are the same as on Windows. By default, `gamedata/`,
`userdata/` and mods live beside the extracted executable, not in the Windows installation.

## Saves and backups

By default, saves and port settings live in `userdata/` beside the executable.
`--data DIR` selects another **whole userdata root**, including `port_settings.ini`;
it does not select game assets. A fresh portable folder does not migrate an existing profile.

- Close the game before backing up or copying the **entire** userdata folder. Keep a separate copy
  before updating, changing APKs or trying developer commands; do not mix individual checkpoint,
  level or `.bak` files from different snapshots.
- Normal saving uses checked per-file atomic replacement and previous-file `.bak` backups.
  This is not a coordinated whole-profile backup, and backups are not restored automatically.
  Structural validation does not guarantee a playable checkpoint or compatibility across versions.
- `--no-save` plays on a temporary copy: autosaves/settings work there, but the original profile
  is unchanged and progress is discarded on exit. **Omit it when you want to keep progress.**
- Do not run multiple builds against the same profile, including older builds that do not respect
  the profile lock. Failures and selected load files are logged as `[save-io]` / `[save-load]`.

See [save/load reliability](docs/SAVE_LOAD_RELIABILITY.md) for guarantees and recovery cautions.

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
| Backtick / ~ | console (see below) |
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
- **Graphics**: **Anti-Aliasing** (MSAA Off/2x/4x/8x), **Renderer** (Legacy / Native), then, for the native renderer (see Rendering below):
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
For opt-in CPU/memory investigation, `--perf` adds bounded private telemetry;
see [Performance diagnostics and user test plan](docs/PERFORMANCE_DIAGNOSTICS.md).
Use `--assets DIR --lib FILE` for matching extracted game files elsewhere and
`--data DIR` for a different userdata root (saves **and** settings).
`--no-save` plays on a disposable copy of that root: autosaves/settings work in
the temporary session, but originals are not updated. Relaunch to return to the
original starting save; this does not freeze checkpoints within a running game.
See [Debug controls and save protection](docs/debug_menu.md) for examples, console
commands, keyboard shortcuts, developer tools and safety limits.

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

Quitting: **Quit** is the last button in the main menu's bottom bar, after Play / Options / Info / Extras.
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

### Rendering
The game draws with OpenGL ES 1.1 (fixed function). Two paths are built in:
- **Legacy**: each GL ES call is translated to desktop OpenGL's own fixed-function pipeline.
- **Native**: the port mirrors the game's GL state, converts every draw's vertex data itself and draws
  it through its own GLSL shaders (modern OpenGL), with optional lighting and post-processing.
  Baseline comparisons use `tools/framediff.py`; these are not a guarantee of pixel-identical output
  in every scene, and enabling enhancements intentionally changes the image.

Switch with F8, the Options row, or `renderer = native|legacy` in port_settings.ini. The legacy
path stays as the fallback.

On a slower PC, try **Legacy** if Native cannot maintain your desired frame rate. Native's lighting
and shadows add work; neither renderer has a universal performance guarantee. Keep the same
resolution, frame cap and scene when comparing them. The default cap is 60 FPS; higher limits
have not been comprehensively validated for gameplay and timing.

Native offers anisotropic texture filtering, improved depth precision, ambient occlusion,
bloom/sharpening, dynamic lights, shadows and specular highlights. Configure these in
Options > Graphics; some depend on driver capabilities. Texture packs go in
`mods/textures/<pack>/` beside the executable.

See [Rendering details](docs/RENDERING.md) for the individual effects, defaults, texture-pack
format and technical implementation. Native CPU vertex/normal preparation is optimized by default;
its reference mode and validation scope are documented in [Native preparation](docs/NATIVE_PREPARATION.md).

### Debug views
Four views, each toggled by a key or a console command (`/debug_off` turns all of them off):
- **Vertex lighting** (F5, `/debug_vertexlight`, cycles): *hidden* draws the level's opaque,
  vertex-coloured meshes without their vertex colours (textures and added lights only); *alone* draws
  them with the vertex colours but no texture. Meshes without vertex colours are unaffected: where
  nothing changes, the lighting is painted into the textures.
- **Lights** (F6, `/debug_lights`): a cross at every light the port adds, within 30 m, in the light's
  colour, with its kind (strip light, alarm spot, RIG health, stasis module, visor, saw, nav line,
  projectile, flash), radius, and shadow map number if it has one. Lit lights get a ring showing their
  radius; alarm spotlights show their axis and cone edges. Dim crosses are strip lights that exist in
  the level but are not lit right now (out of view).
- **Hitboxes** (F7, `/debug_hitboxes`): the bounding box the game reports for each object within 18 m
  (GameObject::getBounds): green player, red enemies, pink severed limbs, orange projectiles, yellow
  everything else, named when close. These are whole-object boxes, not per-limb hit volumes.
- **Interaction** (F10, `/debug_interact`): cyan = the area in front of the player that E checks,
  white = usable objects' zones, orange = kinesis targets, green = what E would pick.

### Console
Press the ~ key to open or close it; Enter runs a line, Up/Down recall earlier ones, Esc closes.
Typing / shows matching commands above the input line (filtered as you type); Tab completes the
highlighted one. Words in a command are joined with _ (spaces work too).
- /spawn_NAME or /spawn_NAME_COUNT: spawns COUNT (1-20) of that enemy in front of you, e.g.
  /spawn_slasher_4. NAME: slasher, lurker, brute, exploder, pregnant, swarm, boss
- /give_all: gives every weapon
- /credits_AMOUNT and /nodes_AMOUNT: **set**, never add, the selected balance
- /rich: **sets** 500000 credits and 200 nodes; /rich_CREDITS_NODES sets both explicitly
- /restore: restores native health/stasis maxima and requests the normal oxygen refill; does not revive
- /enemies or /enemies_RADIUS: read-only nearest 10 native enemies (default 30 metres)
- /god: toggles god mode (no damage)
- /bandana: toggles infinite ammo
- /level_MAP: loads a level, e.g. /level_03_water_01 (type /level_ to list them). Uses the game's
  developer level select, which also moves the current save profile to that level.
- /kill: kills the player (normal death and Continue screen)
- /help, /clear

Resource/restore commands can affect autosaves; use `--no-save` to keep them temporary.
See [the debug guide](docs/debug_menu.md) for limits, ownership checks and oxygen behavior.

### How E picks
A box in front of Isaac (1.8 m deep, 1.4 m wide, floor to above head height) points where the
camera looks. Every usable object has a zone: its 3D bounding box, padded to at least 0.5 m; objects
without a physical body (control panels, stores, power node locks, O2 stations) get a zone built
around the spot where the game shows their tap icon. E uses the one object whose zone overlaps the
box nearest your line of sight, and nothing else. With nothing in reach, E grabs a kinesis target
within ~9 m straight ahead. Plain shoot switches are not used with E (shoot them); covered ones are opened with E.

The nearby-object search measures distance to the interaction zone, not the object's origin.
Controls on large objects (such as a gondola) can therefore be used at the object's edge even when
its origin is far away. The F10 interaction overlay uses the same zone-based distance check.

### Dev mode
Dev command `perf 1` logs slow frames with where the renderer's own CPU time went (lamp search, lamp
facing, shadow-map drawing, still-scene upload). The lamp search is capped at about 2.5 ms per frame and
skips texture areas with nothing bright in them (a 16 x 16 mask per texture).

Lighting survey: `tools/survey.sh LEVEL [COUNT]` stands in front of COUNT doors looking down the corridor
and prints, per stop, brightness / clipped / near-black share with the added lighting off and on and the
lights in use; shots go to `survey/`. `tools/surveysum.py FILES` averages per level. `DS_DEV=name` in
front of any dev script runs it as a separate instance (own command folder `name/`, own profile
`name_data/`), so several levels can be surveyed at once. Do not put MSYS2 on the PATH before calling
the scripts: MSYS2's bash started from Git Bash drops environment variables (the scripts call each other
with `"$BASH"` for the same reason).

`tools/tomenu.sh` starts a fresh profile and stops at the main menu (a fresh profile only has one after a
game exists: it starts one, skips the intro, pauses and quits to the menu; about 40 s).

Faster test loops:

- **Quick start**: `tools/devfresh.sh [--level MAP] [--at "X Y Z"] [--renderer native|legacy]` reaches
  gameplay on a fresh profile in about 6-10 s. It runs the game unthrottled (`turbo 6`: six extra game
  frames per displayed frame, no frame limit) and keeps pressing "continue" until the player has control,
  then turns turbo off. Waits are by state, not by frame count: `waitfor play|menu|loading|notloading|button NAME [MAXMS]`,
  and `tapbtn NAME` taps a menu button by name.
- **Shader hot-reload**: `shaders` recompiles the six post-processing shaders (bloom, AO, final lighting
  pass) from the raw strings in `src/render/native.cpp` on disk, in under a second, with no rebuild or
  restart. A shader that does not compile is reported with the driver's message and the old one stays.
  The per-draw shader (`kVS` / `kFS`) still needs a rebuild.
- **Text instead of pictures**: `report` prints the state (gameplay / menu, highlighted button, usable
  buttons), what the last frame was drawn with (lights by kind, shadow tiles, fog draws, world tint,
  lockdown) and the player's position; `stat [X0 Y0 X1 Y1]` gives mean colour, luma, saturation, clipped
  and near-black share of a screen region (fractions of the screen).
- **Bursts**: `burst NAME [N] [STEP] [DIV]` records N frames STEP frames apart into one filmstrip PNG
  (four per row), for motion and flicker.

Dev command `rich [CREDITS] [NODES]` fills the wallet (default 9999999 credits, 99 power nodes) for store
and bench tests; level 01_gov_03 has both next to each other (`goto Store 0`, `goto Bench 0`, `press e`).

`tools/starttest.py N SECONDS [-- game args]` starts the dev build N times on a throwaway profile and
reports any start that exits by itself, with its exit code. Access violations outside guest memory are
logged at first chance (`access violation (first chance)` plus a host stack).

`tools/dev.py status` says whether the dev instance is running and, if it has exited, shows the crash
report from its log. `send` and `status` exit with code 3 as soon as the game is not running.
`tools/grabtest.sh ENEMY DIST` runs a grab test (slasher, lurker, swarm).
`deadspace.exe --dev DIR` reads commands from `DIR/cmd.txt` while running; `tools/dev.py` drives
it (`start`, `send "cmd; cmd"`, `stop`). Commands are listed at the top of `src/devmode.cpp`
(teleport, goto an object, press keys, screenshots, `look`, `audit`, `god`...).

## Troubleshooting

- **Slow frame rate:** try Legacy in Options > Graphics (or F8), then compare at the same resolution
  and frame cap. Update the GPU driver if OpenGL initialization fails. A feature's availability
  depends on the driver, not just its displayed OpenGL version.
- **Import refused:** read the log, confirm the APK layout is supported, and choose a fresh
  `--game-dir` for different game data. Do not delete a working installation to force an import.
- **Save/load problem:** close other instances and back up the whole profile before investigating.
  Keep checkpoint/level pairs together; do not assume a successful parse proves a correct restore.
- **Crash or other bug:** record the exact build/tag from `BUILD_INFO.txt`, OS/GPU/driver, renderer,
  graphics settings, chapter/location and the actions leading up to it. Copy `deadspace.log` and
  `stderr.log` before the next launch overwrites them. On Windows, keep `crash.dmp` too if created;
  crash capture is best-effort, and a freeze or forced termination may produce no dump.

Use [Issues](../../issues) for a reproducible description. Logs can reveal personal paths, and
dumps can contain game memory: share sensitive evidence privately rather than attaching APKs,
assets, profiles, dumps or recordings to a public issue. `--log FILE` changes the main log's path;
`stderr.log` and Windows crash dumps still go beside the executable.

## Building

See [development and private-data rules](docs/DEVELOPMENT.md) before contributing.
APKs, extracted game assets and saves must never be committed or uploaded with builds.

### Windows (MSYS2 MINGW64)

`external/dynarmic` is a clone of https://github.com/azahar-emu/dynarmic with its submodules
`fmt mcl robin-map xbyak zycore zydis` checked out (both platforms).

```
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,SDL2,boost,zlib}
git submodule update --init --recursive
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

The Windows build statically links SDL2 and the compiler runtimes, so those DLLs do not need to be
shipped separately. Windows system libraries and a working OpenGL driver are still required.

The installer (`installer/installer.cpp`, a small Win32 program with the checked
`deadspace.exe` and notices embedded as resources, and `src/apk.cpp` for the import)
can be built manually from an exact tagged checkout:

```
bash tools/mkinstaller.sh v1.1-rc1 build
```

`setup.exe /silent <folder> <apk>` installs without a window (exit code 0 on success), for tests.

### Linux (x86-64)

```
sudo apt install build-essential cmake ninja-build libsdl2-dev libboost-dev zlib1g-dev libgl-dev
git submodule update --init --recursive
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

### Automated builds and tests

GitHub Actions builds Windows x64 and Linux x64 on pushes and pull requests,
and runs the asset-free CTest suite. Successful runs offer development binaries
under the run's **Artifacts** section (retained for 14 days), with dependency
notices, build information and SHA-256 checksums. These are not tagged releases.
Linux artifacts require the host libraries above; Windows builds are statically
linked. Neither artifacts nor tests contain game data; supply your own APK.

Published releases have a separate workflow that attaches checked Windows/Linux
portable ZIPs and checksums. The Windows installer remains a manual build. See
[release and installer instructions](docs/RELEASING.md), including how to backfill
an already-published tag without moving it.

See [the test and CI instructions](docs/DEVELOPMENT.md) for a lightweight test-only
build that does not require SDL2, OpenGL or Dynarmic.

## Layout

- `src/cpu.*` — dynarmic wrapper, guest threads, host<->guest calls (SVC stubs)
- `src/memory.*` — 4 GiB guest address space, heap, mmap regions
- `src/elf_loader.*` — loads/relocates the ARM shared library, symbolizes addresses
- `src/hle/` — bionic libc, stdio, printf/scanf, pthreads, liblog, GLES 1.1 -> OpenGL, timing
- `src/jni/` — JNIEnv/JavaVM tables and host versions of the Java classes the game calls
- `src/apk.*` — APK (zip) import
- `installer/` — the Windows installer / uninstaller
- `src/render/native.*`, `src/lights.*` — the new renderer: lighting, shadow maps, post-processing
- `src/texpack.*` — texture packs
- `src/input.*`, `src/menu_nav.*` — keyboard, mouse and controller; menu navigation
- `src/sbin.*`, `src/game_patches.*` — patches the game's menu data (SBIN databases): the extra Options
  pages, keyboard / controller wording, the main menu's Quit button
- `src/settings.*` — port settings (`userdata/port_settings.ini`)
- `tools/` — dex disassembler, ARM disassembly helpers, texture/screenshot utilities

## Notes on the APK

The "fan-fixed" APK re-encoded the textures as ETC2 while the game still labels them ATC, and keeps the
original ARM library under `assets/guest_libs`; the port decodes its textures as ETC2. An unmodified APK
(library under `lib/armeabi*`) has real ATC textures: the importer leaves a note (`gamedata/atc_textures`)
and the game then uses the ATC decoder; `--atc-textures` forces it.

## Legal

This is a fan project for people who own the game. The repository and the installer contain none of the
game's code, data or assets; they are read from the user's own APK on the user's machine. Dead Space is a
trademark of Electronic Arts Inc. The port's own source is in this repository; `external/dynarmic` is
under its own licence.
