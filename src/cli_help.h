#pragma once

#include <string_view>

namespace cli {

inline bool help_requested(int argc, char* const argv[]) {
    for (int i = 1; i < argc; i++)
        if (std::string_view(argv[i]) == "--help" || std::string_view(argv[i]) == "-h") return true;
    return false;
}

inline const char* help_text() {
    return R"HELP(Dead Space Mobile PC Port
Usage: deadspace [options]

Game files and profiles:
  -h, --help                 Print this help and exit without starting the game.
  --assets DIR               Extracted assets directory (gamedata/assets).
  --lib FILE                 Matching libDeadSpace.so (gamedata/libDeadSpace.so).
  --apk FILE                 Import an original-layout ARM32 APK (A-D inventory).
  --game-dir DIR             Game-data root for import OR reuse (assets + library).
                             Default: gamedata beside executable. No profile change.
  --import-only              With --apk and explicit --game-dir: extract and exit
                             BEFORE SDL/game/profile/log/crash initialization.
                             Only --apk, --game-dir, --verbose accepted in this mode.
  --data DIR                 Userdata root: saves AND port_settings.ini.
                             Default: userdata beside the executable.
  --no-save                  Play on a disposable copy of that userdata root.
                             Original saves/settings are not changed. Autosaves
                             work in the temporary session; relaunch to reset.

Display and graphics:
  --width W --height H       Game resolution (default: desktop resolution).
  --fullscreen              Start in borderless fullscreen.
  --fps N                   Frame cap for this run; 0 = unlimited.
  --msaa N                  Anti-aliasing: 0, 2, 4 or 8 samples.
  --atc-textures            Force original-APK ATC texture decoding.
                             Renderer selection is in Options > Graphics / F8.

Logging:
  -v, --verbose             Verbose logging.
  --log FILE                Main log destination (default: beside executable).
  --perf                    Opt-in CPU/frame/memory/cache diagnostics, once/second.
                             /perf mark LABEL annotates the private log. No dumps,
                             cache flushing, gameplay changes or GPU timing claims.
  --trace-input             Trace native player/HUD input events.
  --trace-grapple           Trace grapple transitions, hit requests and HP changes.

Developer options (see docs/debug_menu.md):
  --dev DIR                 Enable the file-command interface in DIR.
  --level MAP               Override the next loaded map (not a save editor).
  --teleport CLASS[:x;y;z]   Debug relocation near an object; quote this argument.
  --give-weapons            Debug: give weapons on the first gameplay frame.
  --input SCRIPT            Frame input: FRAME:KEY:VALUE,... (MOUSE uses dx;dy).
  --taps SCRIPT             Touch input: FRAME:X:Y[:HOLD[:DX:DY]],...
  --exit-frame N            Quit after N displayed frames.
  --gltrace FRAME           Log GL calls for a selected frame.
  --shots FRAMES            Capture comma-separated frame numbers.
  --shot-dir DIR            Scripted screenshot directory (default: shots).
  --dump-audio FILE         Dump streamed game audio to FILE.
  --dump-textures DIR       Dump compressed texture payloads to DIR.
  --dump-textures-png        Dump decoded textures into mods/textures/_dump.
  --crash-test              Intentionally crash to test reporting. NOT gameplay.

Example:
  deadspace --apk "D:/DSM/game.apk" --game-dir "D:/DSM/test-A/game" --import-only
  deadspace --game-dir "D:/DSM/test-A/game" --data "D:/DSM/test-A/userdata"
  deadspace --assets "D:/DSM/gamedata/assets" --lib "D:/DSM/gamedata/libDeadSpace.so" --data "D:/DSM/userdata" --no-save

Import writes a fresh staged directory; identical existing data is reused, while
different existing data is refused unchanged (choose a fresh --game-dir).
Reanimated/HD is detected and refused BEFORE extraction; support is planned.
--game-dir cannot be mixed with --assets/--lib; --data is independent. In
--import-only mode redirect stdout to keep import diagnostics; no default log.

--help takes precedence over other arguments and does not import data or write
logs/dumps. --no-save protects the userdata source, not APK imports, logs, texture
or audio dumps you explicitly request. Temporary cleanup is best-effort; a crash
may leave a private deadspace-nosave-* directory in the system temporary folder.
No APK, game assets or saves are included with this port.
)HELP";
}

}  // namespace cli
