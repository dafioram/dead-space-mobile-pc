# Debug controls, console and developer interface

There is no separate port debug-menu screen. There are three interfaces:
keyboard shortcuts, the in-game slash-command console, and an opt-in file-based
developer interface. `deadspace --help` (or `-h`) lists all command-line flags
supported by that executable and exits without loading assets, SDL or the game.

## Protect your saves first

```powershell
.\deadspace.exe --data "D:\DSM\userdata" --no-save
```

`--no-save` **copies the whole userdata root into a private temporary session**.
The game and port settings both use the copy; original save files, profiles and
`port_settings.ini` are not updated by this run. Do not point `--data` at a nested
save subdirectory. No original folder is created if it does not exist.

- Autosaving and checkpoint reload work normally **inside the temporary copy**.
  A checkpoint can therefore advance during this run. Relaunch with `--no-save`
  to start from the unchanged original save again.
- This is not a write-rejection mode or an option to force every Continue action
  to reread the original checkpoint. No native save errors are fabricated.
- It is opt-in for each launch; omit it for normal persistent progress. Settings
  changes in a no-save session are temporary too. Per-file backups created inside
  the copy are disposable; this is not a permanent backup of your originals.
- Close other game instances while starting it if you need a consistent copy.
  It cannot stop another process from writing your originals.
- Symlinks, Windows reparse points/junctions and special files in the source are
  rejected, rather than risking a copy that still refers to real saves. Hard-linked
  files are copied as independent bytes. Copy failure aborts startup: there is no
  fallback to the writable original.
- Scratch directories are named `deadspace-nosave-*` in the OS temporary folder
  (the active path is logged). Cleanup is best-effort. A crash or locked file can
  leave private copied saves there; remove only that session directory when no
  game process is using it. Originals remain protected even if cleanup fails.
- This protects game/settings writes through the userdata root, not arbitrary
  output paths you explicitly supply for logs, audio, texture dumps or APK
  imports. Avoid placing those outputs in your original userdata folder.

To keep assets elsewhere as well:

```powershell
.\deadspace.exe --assets "D:\DSM\gamedata\assets" --lib "D:\DSM\gamedata\libDeadSpace.so" --data "D:\DSM\userdata" --no-save
```

Use matching assets/library from your own APK. `--apk FILE` still imports beside
the executable; it is not an alternate import-destination option.

### Save/load diagnostics

Normal saving remains enabled. Profile `.sb` replacements are staged, checked and
synced; successful publication logs `[save-io] COMMITTED`. Failures identify the
operation/path/errno or structural reason. `[save-load]` records selected files,
content identity and native parsing separately from later restoration/rendering.
An invalid supported file is refused, not silently repaired or replaced by a
backup. Unfamiliar formats are reported as unverified, not blanket-banned.

`<filename>.bak` is the previous validated per-file save, not necessarily a
consistent whole-profile recovery set. `.ds-save-*.tmp` is uncommitted output;
startup reports abandoned stages but never auto-loads/promotes them. The normal
`.deadspace-profile.lock` filename may remain after exit; the OS lock, not file
existence, prevents concurrent writers. Close older builds too: they do not
cooperate with the new lock. Preserve the complete profile/log before attempting
manual recovery, and never blindly mix checkpoint/level files from different runs.

To test persistence across launches, use a copied profile with `--data` **without**
`--no-save`; that flag intentionally discards session progress on exit.

## Keyboard shortcuts and debug views

The debug keys (backtick, F5-F10, F12, Left Alt) are listed with the other bindings in
[CONTROLS.md](CONTROLS.md). Each view below can also be toggled from the console; `/debug_off` turns
all four off.

### Debug views

- **Vertex lighting** (F5, `/debug_vertexlight`, cycles normal / hidden / alone): *hidden* draws the
  level's opaque, vertex-coloured meshes without their vertex colours (textures and added lights only);
  *alone* draws them with the vertex colours but no texture. Meshes without vertex colours are
  unaffected: where nothing changes, the lighting is painted into the textures.
- **Lights** (F6, `/debug_lights`): a cross at every light the port adds, within 30 m, in the light's
  colour, with its kind (strip light, alarm spot, RIG health, stasis module, visor, saw, nav line,
  projectile, flash), radius, and shadow map number if it has one. Lit lights get a ring showing their
  radius; alarm spotlights show their axis and cone edges. Dim crosses are strip lights that exist in
  the level but are not lit right now (out of view).
- **Hitboxes** (F7, `/debug_hitboxes`): the bounding box the game reports for each object within 18 m
  (GameObject::getBounds): green player, red enemies, pink severed limbs, orange projectiles, yellow
  everything else, named when close. These are whole-object boxes, not per-limb hit volumes.
- **Interaction** (F10, `/debug_interact`): cyan = the area in front of the player that E checks,
  white = usable objects' zones, orange = kinesis targets, green = what E would pick. F9 instead writes
  an interaction report (what E would use and every usable object nearby) to `deadspace.log`.
  See [How E picks](CONTROLS.md#how-e-picks).

F8 switches between the Legacy and Native renderers. Renderer effects and overlays can differ between
them. F8 and graphics options may persist settings during a normal run; use `--no-save` for isolated
comparisons. A report or overlay is not a collision or gameplay fix.

## In-game console

Open with backtick/tilde. Enter runs a command, Up/Down recalls history, Tab
completes the first suggestion, and Escape closes it. Typing `/` shows the matching commands above
the input line, filtered as you type. Type `/help` for the list.
Commands are case-insensitive; spaces are accepted in place of underscores.

| Command | Effect / caution |
|---|---|
| `/help` | List available console commands |
| `/clear` | Clear console output |
| `/debug_lights` | Toggle F6 light overlay |
| `/debug_hitboxes` | Toggle F7 hitbox overlay |
| `/debug_interact` | Toggle F10 interaction overlay |
| `/debug_vertexlight` | Cycle F5 painted-light view |
| `/debug_off` | Reset those four debug views; not a reset for cheats or graphics settings |
| `/give_all` | Give every weapon; can affect subsequently saved progress |
| `/credits AMOUNT` | **Set**, never add, credits; integer 0–2,147,483,647 |
| `/nodes AMOUNT` | **Set**, never add, power nodes; same integer bounds |
| `/rich` | **Set** 500,000 credits and 200 nodes, even if current balances are higher |
| `/rich CREDITS NODES` | Set both balances to explicit values; supply both or neither |
| `/restore` | Restore the native suit health/stasis maxima and request the normal native oxygen refill |
| `/enemies [RADIUS]` | Read-only nearest 10 native enemies, including dead/hidden ones; default 30 m, range 0.1–500 m |
| `/god` | Toggle invulnerability; unsuitable for reproducing damage bugs |
| `/bandana` | Toggle infinite ammo |
| `/kill` | Kill the player using the normal death path |
| `/level` | List available playable map IDs from your assets |
| `/level_MAP` | Developer level load, e.g. `/level_01_gov_02`; may affect progress |
| `/spawn_NAME_COUNT` | Spawn 1–20 enemies, e.g. `/spawn_lurker_1`; may disturb scripts |

`/level_MAP` uses the game's developer level select, which also moves the current save profile to that
level; `/level` lists the maps.

Enemy names: `slasher`, `lurker`, `brute`, `exploder`, `pregnant`, `swarm`, `boss`.
Count defaults to 1. Spawn placement uses a simple ground-oriented arrangement;
it is not a faithful way to reproduce a naturally scripted zero-G encounter.
Weapon, health, spawn and level commands require an appropriate loaded game.
They are existing developer tools, not guaranteed safe gameplay features.

Resource examples: `/credits_500000`, `/nodes_200`, and `/rich_500000_200`
are equivalent to their space-separated spellings. These are **set-to** commands:
repeating them does not accumulate resources. `/credits` leaves nodes unchanged;
`/nodes` leaves credits unchanged. Negative, malformed and out-of-range values
are rejected rather than clamped. The upper bound is a debug safety limit, not
a claim about the retail game's wallet cap.

Resource changes and `/restore` require a current living player in ordinary
gameplay, outside object menus, noninteractive scripts and grapple/swarm
takeovers. Requests are applied on the game thread and discarded if the player,
world or native input-reset generation changes before execution. They do not
resurrect the player, unlock suits/weapons, cancel scripts, alter physics or
change god/infinite-ammo mode. **Changes can autosave** to the active profile;
use `--no-save` for temporary testing.

`/restore` uses the actual suit maxima, not a fixed 100 health. Oxygen retains
native eligibility and refill timing; the console reports whether refill started
or was unavailable/not needed. It does not fabricate an instantly full oxygen
bar or clear suffocation/death flags.

`/enemies 30` measures three-dimensional distance and reports class, guest
address, distance, native alive status and the hidden flag. Its summary counts
all positioned native enemies within the radius and shows the nearest 10 so the
console remains readable. It does not guess a universal enemy HP field, kill,
damage, activate or remove anything. No enemy-kill command is added; the older
experimental developer `kill_broken` helper remains unsafe and separate.

## Command-line diagnostics

See `--help` for argument formats and all flags. Useful starting points:

- `--verbose` / `-v`, `--log FILE`, `--trace-input`.
- Some playtest builds also have `--trace-grapple`: native grapple transitions,
  cleanup, requested hits and sampled HP changes. Check that build's help.
  A requested hit is not proof of HP loss; this trace does not suppress damage.
- `--level MAP`, `--give-weapons`, `--teleport 'CLASS:x;y;z'` alter gameplay.
- `--input SCRIPT`, `--taps SCRIPT`, `--exit-frame N`, `--shots FRAMES`,
  `--shot-dir DIR` automate bounded tests. `--input` also bypasses window-focus
  requirements for scripted input; do not use it for ordinary manual play.
- `--gltrace FRAME`, `--dump-audio FILE`, `--dump-textures DIR`,
  `--dump-textures-png` can generate large/private outputs.
- `--crash-test` deliberately crashes. Never use it as a troubleshooting toggle
  while expecting to continue a playthrough.

## File-based developer interface (advanced)

Start with `--dev DIR`; command batches are read from `DIR/cmd.txt`, with output
in `DIR/out.txt`. Commands are one per line, `;` separates commands, `#` starts a
comment. These are **not** slash commands for the visible console.

`tools/dev.py` manages an assistant-owned instance and waits for commands:

```powershell
$env:DS_DEV = "D:\DSM-tests\commands"
$env:DS_EXE = "C:\path\to\deadspace.exe"
python tools/dev.py start --data "D:\DSM\userdata" -- --assets "D:\DSM\gamedata\assets" --lib "D:\DSM\gamedata\libDeadSpace.so" --no-save --input "0:none:0"
python tools/dev.py send "report; pos; look"
python tools/dev.py status
python tools/dev.py stop
```

Run these from the source checkout. Without explicit flags the script has its
own development defaults; do not assume it finds your installed game files.
**Never use `--fresh` or `devfresh.sh` on real userdata:** that helper deletes the
chosen data directory *before* launching the game, so `--no-save` cannot protect
it. Game-thread queries require a loaded/updating player. Use bounded waits and
`status`/`stop`; `stop` targets the helper's recorded instance, not every game.

### Inspection, input and capture commands

| Command | Meaning |
|---|---|
| `report`, `buttons`, `pos`, `grab`, `usingpad` | Game/render state, menu buttons, player pose, grapple state, active input device |
| `look`, `dump [RADIUS] [all]`, `scripts` | E targeting, nearby objects, encounter scripts/spawn zones |
| `peek OBJ OFFSET [f]` | Read a guest word or float; OBJ `0` means the player; addresses/offsets are hex |
| `fadenodes`, `probe X Y Z R` | Fade-effect state / mesh-light probe (`R 0` stops probing) |
| `wait N` | Wait N displayed frames |
| `waitfor play|menu|loading|notloading [MAXMS]` | Wait for a state (default timeout 60 seconds) |
| `waitfor button NAME [MAXMS]` | Wait for a named usable menu button |
| `key NAME 1|0`, `press NAME [FRAMES]` | Hold/release a key or generate a bounded press |
| `lmb|rmb|mmb 1|0`, `click lmb|rmb|mmb [FRAMES]` | Mouse button hold/release or bounded click |
| `mouse DX DY`, `wheel N`, `tap X Y [HOLD]`, `type TEXT` | Relative look, wheel, native touch, text input |
| `tapbtn NAME` | Touch the centre of a currently usable menu button |
| `pad BUTTON 1|0`, `pad AXIS VALUE` | Controller input; axes `lx ly rx ry lt rt`, range -1..1 |
| `shot NAME`, `burst NAME [N] [STEP] [DIV]`, `stat [X0 Y0 X1 Y1]` | Screenshot, filmstrip, or screen-region colour statistics |
| `sync ID`, `quit` | Completion marker / request quit |

Controller buttons: `a b x y back guide start l3 r3 lb rb up down left right`.
Screenshot names ending `_full` request full resolution. Other `shot` captures
are quarter size. Coordinates for `tap` use the game's screen resolution; `stat`
uses fractions of the screen.

### State-changing and rendering commands

| Command | Meaning / caution |
|---|---|
| `tp X Y Z`, `turn DEG`, `face X Z`, `goto CLASS [N] [DIST] [ANGLE]` | Relocate/turn the player (`~` keeps a coordinate; `~N` adds N) |
| `aimat`, `spawn TYPE [DIST]`, `god [1|0]` | Face a living enemy, spawn an enemy, toggle invulnerability |
| `rich [CREDITS] [NODES]`, `health N` | Change inventory or player health |
| `call OBJ SYMBOL`, `audit [DIST]` | Raw native method call / teleport-based interaction audit; expert-only |
| `kill` | Disabled: the existing enemy-kill implementation is known broken |
| `kill_broken` | Exposes that broken hit-event path; can crash. Do not use |
| `turbo N`, `fpslimit N` | Extra simulation frames / persistent FPS setting (`0` disables limit) |
| `renderer native|legacy`, `revz 1|0` | Rendering path / reversed-Z |
| `debugview 1|0 [lights|hitboxes|interact]`, `vertexlight 0|1|2` | Debug overlays / painted-light mode |
| `normals 1|0`, `hudmagenta 1|0` | Native normals / flat-magenta HUD diagnostic |
| `aniso N`, `bloom N`, `sharpen N`, `ao N [debug|x [GAIN [RADIUS]]]` | Filtering and post effects (effect levels 0–3) |
| `dlights N`, `specular N`, `shadows N [debug]`, `lightshadows 1|0` | Native lighting/shadow effects (levels 0–3) |
| `envshadows 1|0`, `envlight N`, `lightshade N`, `fanshade N` | Fixture shadows, contrast (0–3), shadow strengths (0–1) |
| `glowgain N`, `vignette 1|0`, `fogveil 1|0` | Glow, vignette, fog-light veil |
| `light [R G B [RADIUS [MS [FWD UP SIDE]]]]` | Temporary test light near the player |
| `tint RRGGBB [AMOUNT]`, `tint off`, `shownodes X Z [RADIUS]` | World tint / unhide level nodes |
| `lighttrace 1|0`, `perf 1|0`, `gltrace 1|0`, `shaders [PATH]` | Trace lights/performance/GL or reload post shaders from source |

### Developer workflow scripts

These helpers live in `tools/` and are for people working from a source checkout. Like `dev.py`, they
assume disposable profiles. `DS_DEV=name` in front of any dev script runs it as a separate instance (own
command folder `name/`, own profile `name_data/`), so several levels can be examined at once. Do not put
MSYS2 on the PATH before calling the scripts: MSYS2's bash started from Git Bash drops environment
variables (the scripts call each other with `"$BASH"` for the same reason).

- **Quick start**: `tools/devfresh.sh [--level MAP] [--at "X Y Z"] [--renderer native|legacy]` reaches
  gameplay on a fresh profile in about 6-10 s. It runs the game unthrottled (`turbo 6`: six extra game
  frames per displayed frame, no frame limit) and keeps pressing "continue" until the player has control,
  then turns turbo off. Waits are by state, not by frame count (`waitfor`, `tapbtn`). It deletes its
  data directory first; never point it at real userdata.
- **Main menu**: `tools/tomenu.sh` starts a fresh profile and stops at the main menu (a fresh profile only
  has one after a game exists: it starts one, skips the intro, pauses and quits to the menu; about 40 s).
- **Lighting survey**: `tools/survey.sh LEVEL [COUNT]` stands in front of COUNT doors looking down the
  corridor and prints, per stop, brightness / clipped / near-black share with the added lighting off and on
  and the lights in use; shots go to `survey/`. `tools/surveysum.py FILES` averages per level.
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
- **Performance**: `perf 1` logs slow frames with where the renderer's own CPU time went (lamp search, lamp
  facing, shadow-map drawing, still-scene upload). The lamp search is capped at about 2.5 ms per frame and
  skips texture areas with nothing bright in them (a 16 x 16 mask per texture).
- **Store and bench tests**: `rich [CREDITS] [NODES]` fills the wallet (default 9999999 credits, 99 power
  nodes); level 01_gov_03 has both next to each other (`goto Store 0`, `goto Bench 0`, `press e`).
- **Start and crash checks**: `tools/starttest.py N SECONDS [-- game args]` starts the dev build N times on
  a throwaway profile and reports any start that exits by itself, with its exit code. Access violations
  outside guest memory are logged at first chance (`access violation (first chance)` plus a host stack).
  `tools/dev.py status` says whether the dev instance is running and, if it has exited, shows the crash
  report from its log; `send` and `status` exit with code 3 as soon as the game is not running.
- **Grabs**: `tools/grabtest.sh ENEMY DIST` runs a grab test (slasher, lurker, swarm).

Commands are listed at the top of `src/devmode.cpp`.

## Privacy and interpretation

Never commit or attach APKs, game libraries, assets, saves, recordings, texture
dumps, minidumps or private diagnostic logs to public releases/test fixtures.
Debug commands can change timing or create artificial states; repeat suspected
bugs without cheats before calling them reproduced. A successful build or a
state dump alone is not gameplay confirmation.
