#include "apk.h"
#include "log.h"
#include <cstdio>
#include <string_view>

namespace apk {
std::optional<int> import_cli(int argc, char* const argv[]) {
    bool requested = false;
    for (int i = 1; i < argc; i++)
        if (std::string_view(argv[i]) == "--import-only") requested = true;
    if (!requested) return std::nullopt;
    std::string source, dest;
    bool seen = false;
    auto usage = [](const char* error) {
        std::fprintf(stderr, "Import-only: %s\nUsage: deadspace --apk FILE --game-dir NEW_DIR --import-only [--verbose]\n"
                             "No game/profile initialization. Import messages go to stdout; redirect to keep a log.\n", error);
        return std::optional<int>{2};
    };
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg == "--import-only") {
            if (seen) return usage("duplicate --import-only");
            seen = true;
        } else if (arg == "--verbose" || arg == "-v") {
            logging::g_verbose = true;
        } else if (arg == "--apk" || arg == "--game-dir") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--") || !*argv[i + 1])
                return usage("missing APK or game-directory value");
            auto& value = arg == "--apk" ? source : dest;
            if (!value.empty()) return usage("duplicate source/destination option");
            value = argv[++i];
        } else return usage("only --apk, --game-dir and --verbose are accepted with --import-only (no gameplay/debug flags)");
    }
    if (source.empty() || dest.empty()) return usage("--apk and explicit --game-dir are required");
    LOGI("[apk-import] import-only: no SDL, guest execution, profile, default log or crash handler");
    return apk::import(source, dest) ? 0 : 1;
}
}  // namespace apk
