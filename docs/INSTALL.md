# Installation, saves and requirements

Detailed install notes. The short version is in the [README](../README.md). Building from source is in [DEVELOPMENT.md](DEVELOPMENT.md#building-from-source).

## Installing (Windows)

1. Download the Windows installer, `DeadSpaceMobilePCPort-TAG-Setup.exe`, from the
   [Releases](../../../releases) page and run it.
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
- **Which APK**: several original-layout ARM32 APKs passed user-led gameplay checks, but that does
  not establish compatibility with every APK or every existing save. **Reanimated/HD is detected but
  not imported yet**; it fails before extraction, without changing existing game data. See
  [APK import and isolated tests](APK_IMPORT.md) and the [exact-copy inventory](ANDROID_ASSET_SUPPORT.md).
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

To test several APKs without mixing their data, use `--import-only`, `--game-dir` and `--data`
as described in [APK import and isolated tests](APK_IMPORT.md). Those test folders contain game
assets: never distribute them.

### Requirements

Windows 10 or 11, 64-bit; a graphics card and driver with OpenGL 3.0 or newer for the Native renderer
(Legacy is available in Options > Graphics). Use a driver providing desktop OpenGL compatibility support;
some Native enhancements require additional capabilities. Allow about 400 MB for the installed game,
plus space for your APK, staged imports, saves and any texture packs. A controller is optional: any pad SDL
recognises works, with the Dead Space remake's layout by default.

## Installing (Linux)

Linux x86-64 portable builds are available on the [Releases](../../../releases) page;
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
[Building from source](DEVELOPMENT.md#building-from-source) to build against your system's libraries.
Controls and game-data/profile options are the same as on Windows. By default, `gamedata/`,
`userdata/` and mods live beside the extracted executable, not in the Windows installation.

## Saves and backups

By default, saves and port settings live in `userdata/` beside the executable. `--data DIR` selects
another **whole userdata root**, including `port_settings.ini`; it does not select game assets. A fresh
portable folder does not migrate an existing profile.

- Close the game before backing up or copying the **entire** userdata folder. Keep a separate copy
  before updating, changing APKs or trying developer commands; do not mix individual checkpoint,
  level or `.bak` files from different snapshots.
- Normal saving uses checked per-file atomic replacement and previous-file `.bak` backups. Backups are
  not a coordinated whole-profile backup and are not restored automatically.
- `--no-save` plays on a temporary copy and discards progress on exit. **Omit it when you want to keep
  progress.** See [Debug controls and save protection](debug_menu.md).
- Do not run multiple builds against the same profile, including older builds that do not respect
  the profile lock.

Guarantees, log lines (`[save-io]` / `[save-load]`) and recovery cautions are in
[save/load reliability](SAVE_LOAD_RELIABILITY.md).
