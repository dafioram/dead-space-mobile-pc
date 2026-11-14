#pragma once

#include <filesystem>
#include <system_error>

// Copies, never links, the supplied userdata root. The game and settings layer
// both use data_path(); no guest filesystem entry point retains the source root.
class TemporaryProfile {
    std::filesystem::path root;

public:
    TemporaryProfile() = default;
    TemporaryProfile(const TemporaryProfile&) = delete;
    TemporaryProfile& operator=(const TemporaryProfile&) = delete;
    ~TemporaryProfile();

    void prepare(const std::filesystem::path& source,
                 const std::filesystem::path& temporary_parent = std::filesystem::temp_directory_path());
    std::filesystem::path data_path() const;
    std::error_code discard();
};
