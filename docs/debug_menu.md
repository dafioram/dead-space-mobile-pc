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

### Wall-streak geometry capture (opt-in)

`/geom on` arms the shared Native/Legacy geometry observer; close the console,
then press **F4** when a streak is visible. Wait for **REPORT READY** (about one
second). F8 remains the renderer switch. `/geom mark` also marks from the console,
but F4 avoids typing away the recent combat frames. `/geom status` shows state.
The bounded report goes into `deadspace.log`; preserve it before the next launch.

Optional `/geom focus 1` through `/geom focus 12` highlights the latest report's
ranked candidates: yellow longest sampled triangle, magenta thinnest. These are
**x-ray outlines**, not proof of actual visibility or corruption. Say which number
overlaps the real streak; no normal draw is hidden. `/geom focus 0` clears it;
`/geom off` stops observation. `/debug_off` also turns it off. Capture has CPU
overhead while armed and is disabled by default. It does not change saves.

Full implementation limits and interpretation: `docs/GEOMETRY_DIAGNOSTICS.md` in
the source repository. Send the log and a short recording around F4 markers; keep
all real geometry excerpts/recordings private, not in commits or public packages.

## Keyboard shortcuts

| Key | Action |
|---|---|
| Backtick / tilde | Open or close the command console |
| F5 | Cycle painted vertex lighting: normal / hidden / alone |
| F6 | Toggle light-placement overlay |
| F7 | Toggle hitbox overlay |
| F8 | Switch Legacy / Native renderer |
| F9 | Log an interaction report: what E would select |
| F10 | Toggle interaction volumes: cyan reach, white usable, green selected |
| F11 / Alt+Enter | Toggle fullscreen |
| F12 | Save a screenshot in `shots` beside the executable |
| Left Alt (hold) | Release gameplay mouse capture; use the native touch pointer |

Renderer effects/overlays can differ between Native and Legacy. F8 and graphics
options may persist settings during a normal run; use `--no-save` for isolated
comparisons. A report or overlay is not a collision or gameplay fix.

## In-game console

Open with backtick/tilde. Enter runs a command, Up/Down recalls history, Tab
completes the first suggestion, and Escape closes it. Type `/help` for the list.
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

## Privacy and interpretation

Never commit or attach APKs, game libraries, assets, saves, recordings, texture
dumps, minidumps or private diagnostic logs to public releases/test fixtures.
Debug commands can change timing or create artificial states; repeat suspected
bugs without cheats before calling them reproduced. A successful build or a
state dump alone is not gameplay confirmation.
