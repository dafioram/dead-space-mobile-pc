#include "asset_file.h"
#include "host_file.h"

#include <filesystem>

namespace assets {

std::FILE* open_file(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(host_file::path(path), ec)) return nullptr;
    return host_file::open(path, "rb");
}

}  // namespace assets
