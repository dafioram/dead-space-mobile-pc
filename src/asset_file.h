#pragma once

#include <cstdio>
#include <string>

namespace assets {
// Android AssetManager.open accepts files, not directories. Caller owns the FILE.
std::FILE* open_file(const std::string& path);
}  // namespace assets
