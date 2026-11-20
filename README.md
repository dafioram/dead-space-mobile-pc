# Dead Space (2011 mobile) — native PC port

An unofficial, fan-made port of EA / IronMonkey's 2011 Android game *Dead Space* for Windows and Linux
(x86-64). It runs the game's own 32-bit ARM code on the dynarmic JIT, with everything Android provides
reimplemented on the host using SDL2 and desktop OpenGL. No emulator needed. The game is set between
Dead Space 1 and 2.

**No game data is included.** You need your own copy of the Android game as an APK file, and no link to
one is provided. This project is not affiliated with or endorsed by Electronic Arts; Dead Space is a
trademark of Electronic Arts Inc.

This README describes current `main`. Downloaded releases contain the code from their tagged commit,
which may be older; check the release notes and the included `BUILD_INFO.txt`.

## Highlights

- A real PC experience, not a touch interface mapped to a mouse
- Full keyboard + mouse and controller support
- Game is complete
- Surround sound audio
- New Game+
- Checkpoint save system, and works with original saved games
- Runs smoothly on low-end desktops
- Windows and Linux (x86-64) Support
- Optional Native renderer: dynamic lights, shadows, ambient occlusion, bloom, anti-aliasing, texture packs
- Higher frame rates, skippable cutscenes, Quit button, extended Options menu
- In-game dev options (god mode, spawn enemies, level select, ...)
- Many bug fixes and enhancements over the original

## Install

**Windows:** download `DeadSpaceMobilePCPort-TAG-Setup.exe` from [Releases](../../releases), run it and
pick your APK when asked (about 400 MB installed). Start the game from the Start menu.
A portable ZIP is also available.

**Linux:** download `deadspace-TAG-linux-x86_64-portable.zip`, extract it, install the host libraries
(`sudo apt install libsdl2-2.0-0 libgl1 zlib1g libstdc++6` on Ubuntu/Debian) and run:

```sh
chmod +x deadspace
./deadspace --apk "$HOME/Games/A.apk"   # first run only
./deadspace
```

The Windows binaries are not code-signed, so Windows or your antivirus may warn about an unknown
publisher or report a false positive.

Requirements: Windows 10/11 64-bit, or Linux x86-64, with a GPU and driver supporting OpenGL 3.0+ for the
Native renderer (Legacy is available in Options > Graphics). A controller is optional.

**APK support:** several original-layout ARM32 APKs have passed gameplay checks, but that does not
guarantee every APK works. **Reanimated/HD is detected but not supported yet.**
See [APK support](docs/APK_IMPORT.md).

More detail (updating, uninstalling, portable mode, isolated APK testing, Linux notes):
[docs/INSTALL.md](docs/INSTALL.md).

## Saves

Saves and settings live in `userdata/` beside the executable. Close the game and back up the whole
folder before updating or trying developer commands. `--no-save` plays on a temporary copy and discards
progress on exit. Details: [save/load reliability](docs/SAVE_LOAD_RELIABILITY.md).

## Controls

Mouse and keyboard, or any controller SDL recognises (Dead Space remake layout by default).
Main controls are rebindable in Options > Controls / Controller.
See the full reference in [docs/CONTROLS.md](docs/CONTROLS.md).

## Common options

`--apk FILE`, `--fullscreen`, `--width W --height H`, `--fps N`, `--msaa N`, `--data DIR`, `--no-save`,
`--log FILE`, `--verbose`. Run `deadspace --help` for the full list.

## Troubleshooting

- **Slow frame rate:** try Legacy in Options > Graphics (or press F8) and update your GPU driver.
- **Import refused:** read the log and confirm the APK layout is supported.
- **Crash or bug:** open an [issue](../../issues) and include your `deadspace.log` (and `crash.dmp` on
  Windows). Logs can contain personal paths; never attach APKs, game assets, saves or dumps to a public issue.

## Status and roadmap

Play-tested start to finish on a low-end Windows PC with no major issues, and no known way to crash it
through normal play. Linux has had much less testing. There are a few minor graphical and audio glitches
that are infrequent and don't affect gameplay. New Game+ works but is not extensively tested. 
Linux tested with X11 on Linux Mint 22.3 and Wayland tested on Ubuntu 26.04.

Rough order, not firm:

1. Address graphical and audio glitches
2. Polish the Linux version
3. Support the Android Reanimated/HD version
4. Mac ARM and Linux ARM builds
5. More New Game+ testing
6. Fully understand the save game structure
7. More save content
8. iOS version
9. Mods and enhancements

## More documentation

- [Installation, saves and uninstalling](docs/INSTALL.md)
- [Controls, options and how E picks](docs/CONTROLS.md)
- [Rendering: renderers, performance and Native effects](docs/RENDERING.md), [Native preparation](docs/NATIVE_PREPARATION.md)
- [Debug views, console, command line and developer interface](docs/debug_menu.md)
- [APK import and isolated tests](docs/APK_IMPORT.md), [APK inventory](docs/ANDROID_ASSET_SUPPORT.md)
- [Save/load reliability](docs/SAVE_LOAD_RELIABILITY.md)
- [Building from source, tests, CI and source layout](docs/DEVELOPMENT.md), [Releasing](docs/RELEASING.md)
- Design notes for individual fixes: [Quit button](docs/MAIN_MENU_QUIT.md),
  [navigation line](docs/DIRECT_LOCATOR.md), [menu movement handoff](docs/MENU_MOVEMENT_HANDOFF.md),
  [cinematic input](docs/NONINTERACTIVE_CINEMATICS.md), [death menu input](docs/DEATH_MENU_INPUT.md),
  [RIG credits](docs/RIG_CREDITS.md), [debug console tools](docs/DEBUG_CONSOLE_TOOLS.md),
  [scripted input ownership](docs/SCRIPTED_INPUT_OWNERSHIP.md), [store list lifetime](docs/STORE_LIST_LIFETIME.md),
  [scene cache lifetime](docs/SCENE_CACHE_LIFETIME.md), [zero-G camera](docs/ZERO_G_CAMERA.md) and
  [traversal](docs/ZERO_G_TRAVERSAL.md), [menu widget safety](docs/MENU_WIDGET_SAFETY.md),
  [MSAA playtest](docs/MSAA_PLAYTEST.md), [runtime hardening playtest](docs/RUNTIME_HARDENING_PLAYTEST.md)

Pull requests are welcome. Read the [development and private-data rules](docs/DEVELOPMENT.md) first. Never attach APKs, assets, saves or dumps.

## Legal

This is a fan project for people who own the game. The repository and the installer contain none of the
game's code, data or assets; they are read from the user's own APK on the user's machine. Dead Space is a
trademark of Electronic Arts Inc. The port's own source is in this repository; `external/dynarmic` is
under its own licence.
