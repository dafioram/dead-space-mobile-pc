// Relative and privacy-safe path formatting for logs.
// Rewrites paths inside the app directory or active userdata directory to
// relative paths, and collapses the user's home directory to "~" so shared
// logs do not leak OS usernames or local directory trees.
#pragma once

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#include "host_file.h"

namespace logging {

namespace detail {

inline std::mutex& path_root_mutex() {
    static std::mutex m;
    return m;
}
inline std::string& app_root_storage() {
    static std::string s;
    return s;
}
inline std::string& data_root_storage() {
    static std::string s;
    return s;
}

inline std::string normalize_path(std::string_view raw) {
    std::string s;
    s.reserve(raw.size());
    bool prev_slash = false;
    for (size_t i = 0; i < raw.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(raw[i]);
        char c = (ch < 32 || ch == 127) ? '?' : char(ch);
        if (c == '\\') c = '/';
        if (c == '/') {
            // Preserve leading "//" for UNC paths, collapse duplicate slashes elsewhere.
            if (prev_slash && !(i == 1 && s.size() == 1 && s[0] == '/')) continue;
            prev_slash = true;
        } else {
            prev_slash = false;
        }
        s.push_back(c);
    }
    while (s.size() > 1 && s.back() == '/') {
        // Keep root "/" or Windows drive root "C:/".
        if (s.size() == 3 && s[1] == ':') break;
        s.pop_back();
    }
    return s;
}

inline bool path_char_equal(char a, char b) {
#ifdef _WIN32
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
#else
    return a == b;
#endif
}

// If `path` is equal to or under `root`, writes the relative remainder into `rel` and returns true.
inline bool strip_prefix(std::string_view path, std::string_view root, std::string& rel) {
    if (root.empty() || path.size() < root.size()) return false;
    for (size_t i = 0; i < root.size(); ++i) {
        if (!path_char_equal(path[i], root[i])) return false;
    }
    if (path.size() == root.size()) {
        rel.clear();
        return true;
    }
    if (path[root.size()] == '/') {
        rel.assign(path.substr(root.size() + 1));
        return true;
    }
    return false;
}

inline std::string normalize_root(const std::string& root) {
    if (root.empty()) return {};
    std::error_code ec;
    std::filesystem::path p = host_file::path(root);
    std::filesystem::path norm = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        ec.clear();
        norm = std::filesystem::absolute(p, ec).lexically_normal();
    }
    return normalize_path(ec ? root : host_file::utf8(norm));
}

inline std::string default_home_dir() {
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE"); p && *p) return normalize_path(p);
    const char* drive = std::getenv("HOMEDRIVE");
    const char* path = std::getenv("HOMEPATH");
    if (drive && *drive && path && *path) return normalize_path(std::string(drive) + path);
#endif
    if (const char* p = std::getenv("HOME"); p && *p) return normalize_path(p);
    return {};
}

}  // namespace detail

inline void set_path_roots(const std::string& app_root, const std::string& data_root = {}) {
    std::lock_guard lk(detail::path_root_mutex());
    detail::app_root_storage() = detail::normalize_root(app_root);
    if (!data_root.empty()) detail::data_root_storage() = detail::normalize_root(data_root);
}

inline void set_profile_root(const std::string& data_root) {
    std::lock_guard lk(detail::path_root_mutex());
    detail::data_root_storage() = detail::normalize_root(data_root);
}

inline void clear_path_roots() {
    std::lock_guard lk(detail::path_root_mutex());
    detail::app_root_storage().clear();
    detail::data_root_storage().clear();
}

// Formats a filesystem path for log output:
//  1. Inside app_root  -> relative path ("." or "gamedata/assets", "userdata/...")
//  2. Inside data_root -> "userdata" or "userdata/..."
//  3. Inside home_dir  -> "~" or "~/..."
//  4. Otherwise        -> normalized, control-char-sanitized path
inline std::string display_path(const std::string& raw, const std::string& home_override = {}) {
    const std::string norm = detail::normalize_path(raw);
    if (norm.empty()) return norm;
    std::string app_root, data_root;
    {
        std::lock_guard lk(detail::path_root_mutex());
        app_root = detail::app_root_storage();
        data_root = detail::data_root_storage();
    }
    std::string rel;
    if (detail::strip_prefix(norm, app_root, rel)) return rel.empty() ? "." : rel;
    if (detail::strip_prefix(norm, data_root, rel)) return rel.empty() ? "userdata" : ("userdata/" + rel);
    const std::string home = home_override.empty() ? detail::default_home_dir() : detail::normalize_path(home_override);
    if (detail::strip_prefix(norm, home, rel)) return rel.empty() ? "~" : ("~/" + rel);
    return norm;
}

inline std::string display_path(const std::filesystem::path& p, const std::string& home_override = {}) {
    return display_path(host_file::utf8(p), home_override);
}

}  // namespace logging
