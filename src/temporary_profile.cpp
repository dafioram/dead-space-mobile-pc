#include "temporary_profile.h"

#include <random>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

void require_plain_entry(const fs::path& path, const fs::file_status& status) {
#ifdef _WIN32
    // Junctions/reparse directories must not evade the portable symlink check.
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw std::runtime_error("temporary profile cannot copy a reparse/inaccessible entry: " + path.string());
#endif
    if (fs::is_symlink(status) || (!fs::is_regular_file(status) && !fs::is_directory(status)))
        throw std::runtime_error("temporary profile refuses links or special files: " + path.string());
}

}  // namespace

TemporaryProfile::~TemporaryProfile() { discard(); }

void TemporaryProfile::prepare(const fs::path& requested_source, const fs::path& temporary_parent) {
    if (!root.empty()) throw std::logic_error("temporary profile already prepared");
    auto source = fs::absolute(requested_source).lexically_normal();
    std::error_code error;
    auto status = fs::symlink_status(source, error);
    bool present = fs::exists(status);
    if (error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("inspect userdata", source, error);
    if (present) {
        require_plain_entry(source, status);
        if (!fs::is_directory(status)) throw std::runtime_error("userdata root is not a directory: " + source.string());
        // Do not create the destination inside the source being copied (e.g.
        // --data pointing at the OS temp root), even under differently-cased paths.
        for (auto parent = fs::canonical(temporary_parent);; parent = parent.parent_path()) {
            if (fs::equivalent(parent, source)) throw std::runtime_error("temporary profile destination would be inside userdata");
            if (parent == parent.root_path()) break;
        }
    }

    try {
        std::random_device random;
        for (int attempt = 0; attempt < 32; attempt++) {
            auto candidate = temporary_parent / ("deadspace-nosave-" + std::to_string(random()) + "-" + std::to_string(random()));
            // Atomic directory creation: never reuse or remove a pre-existing path.
            if (fs::create_directory(candidate)) {
                root = candidate;
                break;
            }
        }
        if (root.empty()) throw std::runtime_error("cannot allocate a unique temporary profile directory");
        fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);
        fs::create_directory(data_path());
        if (!present) return;  // empty session; do not create the original root

        for (const auto& entry : fs::recursive_directory_iterator(source)) {
            auto entry_status = entry.symlink_status();
            require_plain_entry(entry.path(), entry_status);
            auto destination = data_path() / entry.path().lexically_relative(source);
            if (fs::is_directory(entry_status)) {
                fs::create_directory(destination);
            } else {
                fs::copy_file(entry.path(), destination);
                fs::permissions(destination, fs::perms::owner_write, fs::perm_options::add);
            }
        }
    } catch (...) {
        discard();
        throw;  // fail closed, never fall back to the real userdata root
    }
}

fs::path TemporaryProfile::data_path() const { return root.empty() ? fs::path{} : root / "userdata"; }

std::error_code TemporaryProfile::discard() {
    std::error_code error;
    if (!root.empty()) {
        fs::remove_all(root, error);  // only the unique directory this instance created
        if (!error) root.clear();
    }
    return error;
}
