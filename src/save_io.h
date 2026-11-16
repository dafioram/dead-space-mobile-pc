// Checked host profile I/O. Transactions are per file, not a whole-profile journal.
#pragma once
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace save_io {
using Path = std::filesystem::path;
Path utf8_path(const std::string& path);
std::string path_text(const Path& path);
std::string path_utf8(const Path& path);
bool profile_file(const Path& path);
void set_profile_root(const Path& root);

int open(const Path& path, int flags, int mode = 0644);
int close(int fd);
int64_t read(int fd, void* data, size_t size);
int64_t write(int fd, const void* data, size_t size);
int64_t seek(int fd, int64_t offset, int origin);
int sync(int fd);
int truncate(int fd, uint64_t size);

std::FILE* fopen(const Path& path, const std::string& mode);
int fclose(std::FILE* stream);
size_t fread(void* data, size_t size, size_t count, std::FILE* stream);
size_t fwrite(const void* data, size_t size, size_t count, std::FILE* stream);
int fflush(std::FILE* stream);
void stream_error(std::FILE* stream, const char* operation);
void fail_stream(std::FILE* stream, const char* operation, int error);
void fail_fd(int fd, const char* operation, int error);
bool stream_failed(std::FILE* stream);
void clear_stream_error(std::FILE* stream);

// The native filename-save wrapper starts/finishes these scopes. Closed staged
// files are published only if serialization AND all I/O/commit stages succeed.
void begin_save();
bool finish_save(bool serialized);
std::string current_load();
struct LoadInfo { std::string path; bool structurally_invalid = false; };
LoadInfo load_info(int fd);
bool tracked_profile_fd(int fd);
bool failed_fd(int fd);
uint64_t failure_epoch();
bool atomic_text(const Path& path, std::string_view text);
void describe_library(const Path& path);

class ProfileLock {
    intptr_t handle_ = -1;
public:
    ProfileLock() = default;
    ProfileLock(const ProfileLock&) = delete;
    ProfileLock& operator=(const ProfileLock&) = delete;
    ~ProfileLock();
    bool acquire(const Path& root);
    void release();
};

// Dependency injection for synthetic fault tests only. No CLI/environment
// switch can activate this in the application; nullptr restores real I/O.
enum class Stage { data_synced, stream_closed, backup_ready, final_replaced };
struct Faults {
    size_t write_limit = 0;
    size_t bytes_until_error = SIZE_MAX;
    unsigned interrupted_writes = 0;
    bool zero_write = false, write_error = false, sync_error = false;
    bool close_error = false, replace_error = false, directory_sync_error = false;
    bool backup_error = false, read_error = false;
    bool flush_error = false;
    void (*after_stage)(Stage) = nullptr;
};
void test_faults(Faults* faults);
}  // namespace save_io
