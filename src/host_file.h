#pragma once
#include <cstdio>
#include <filesystem>
#include <string>

// Host-only I/O, not guest stdio or transactional profile I/O. Path strings at
// the port boundary are UTF-8; do not pass them to the Windows ANSI CRT APIs.
namespace host_file {
inline std::filesystem::path path(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
inline std::string utf8(const std::filesystem::path& native) {
    auto text = native.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}
inline std::FILE* open(const std::string& name, const char* mode) {
#ifdef _WIN32
    return _wfopen(path(name).c_str(), path(mode).c_str());
#else
    return std::fopen(name.c_str(), mode);
#endif
}
inline std::FILE* reopen(const std::string& name, const char* mode, std::FILE* stream) {
#ifdef _WIN32
    return _wfreopen(path(name).c_str(), path(mode).c_str(), stream);
#else
    return std::freopen(name.c_str(), mode, stream);
#endif
}
}  // namespace host_file
