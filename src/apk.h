#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace apk {
struct ImportFaults {  // synthetic tests only; no application CLI/env switch
    size_t bytes_before_error = SIZE_MAX;
    bool close_error = false, publish_error = false;
};
// Fresh, no-replace directory publication; identical existing data is reused.
// Different existing data, reparse paths and Reanimated/HD are refused unchanged.
// Extracts assets/ and the ARM32 library into dest_dir. Returns false on failure.
bool import(const std::string& apk_path, const std::string& dest_dir, ImportFaults* faults = nullptr);
// Handles --import-only before SDL/log/profile/game initialization. nullopt means
// normal startup. Import-only accepts --apk, --game-dir and --verbose only.
std::optional<int> import_cli(int argc, char* const argv[]);
}  // namespace apk
