#include "asset_file.h"

#include <filesystem>

namespace assets {

std::FILE* open_file(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return nullptr;
    return std::fopen(path.c_str(), "rb");
}

}  // namespace assets
