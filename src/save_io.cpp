#include "save_io.h"
#include "save_validation.h"
#include "log.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

namespace save_io {
namespace fs = std::filesystem;
Path utf8_path(const std::string& path) { return Path(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size())); }
std::string path_utf8(const Path& path) {
    auto s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}
std::string path_text(const Path& path) {
    std::string text = path_utf8(path);
    for (char& c : text) if (static_cast<unsigned char>(c) < 32 || c == 127) c = '?';
    return text;
}
bool profile_file(const Path& path) {
    auto extension = path.extension().native();
    if (extension.size() != 3 || extension[0] != '.') return false;
    return (extension[1] == 's' || extension[1] == 'S') && (extension[2] == 'b' || extension[2] == 'B');
}

namespace {
constexpr uint64_t inspect_limit = 64ull * 1024 * 1024;
struct Scope;
struct Entry {
    std::mutex mutex;
    Path path, temporary;
    int fd = -1, error = 0;
    bool tagged = false, transaction = false, text = false, closed = false, reading = false;
    uint64_t written = 0, read = 0;
    std::FILE* stream = nullptr;
    std::weak_ptr<Scope> owner;
    bool deferred = false;
    bool load_invalid = false;
    bool stream_error = false;
    ~Entry();
};
struct Scope { std::atomic<bool> failed{false}; std::vector<std::shared_ptr<Entry>> opened; };
thread_local std::vector<std::shared_ptr<Scope>> scopes;
thread_local uint64_t failures = 0;
thread_local std::vector<std::pair<int, std::string>> loads;
thread_local Faults* faults = nullptr;
std::mutex registry_mutex;
std::map<int, std::shared_ptr<Entry>> entries;
std::map<std::FILE*, std::shared_ptr<Entry>> streams;
struct PathLess {
    bool operator()(const Path& a, const Path& b) const {
#ifdef _WIN32
        int result = CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE);
        if (result) return result == CSTR_LESS_THAN;
#endif
        return a < b;
    }
};
std::map<Path, std::weak_ptr<Entry>, PathLess> writers;
Path profile_root;
std::atomic<uint64_t> serial{0};
void stage(Stage reached) { if (faults && faults->after_stage) faults->after_stage(reached); }
int filesystem_error(const std::error_code& error) {
    for (int code : {ENOENT, EACCES, ENOSPC, EROFS, ENOTDIR, EISDIR, EEXIST, ENOTEMPTY, EBUSY, EINVAL})
        if (error == std::error_condition(code, std::generic_category())) return code;
    return EIO;
}

void failure(Entry& entry, const char* operation, int error, uint64_t requested = 0, uint64_t completed = 0) {
    if (!error) error = EIO;
    if (entry.stream) entry.stream_error = true;
    ++failures;
    if (entry.transaction && !entry.error) entry.error = error;
    if (entry.transaction) {
        if (auto scope = entry.owner.lock()) scope->failed = true;
        else if (!entry.text && !scopes.empty()) scopes.back()->failed = true;
    }
    LOGE("[save-io] %s FAILED path=%s errno=%d (%s) requested=%llu completed=%llu prior-final=%s",
         operation, path_text(entry.path).c_str(), error, std::strerror(error),
         (unsigned long long)requested, (unsigned long long)completed, entry.transaction ? "not truncated" : "not transactional");
    errno = error;
}
int os_open(const Path& path, int flags, int mode) {
#ifdef _WIN32
    return _wopen(path.c_str(), flags | O_BINARY | O_NOINHERIT, mode);
#else
    return ::open(path.c_str(), flags | O_CLOEXEC, mode);
#endif
}
int os_close(int fd) {
#ifdef _WIN32
    int r = _close(fd);
#else
    int r = ::close(fd);
#endif
    // A failing close must not be retried: on POSIX the fd may already be freed.
    if (faults && faults->close_error) { errno = EIO; return -1; }
    return r;
}
int os_sync(int fd) {
    if (faults && faults->sync_error) { errno = EIO; return -1; }
    int r;
    do {
#ifdef _WIN32
        r = _commit(fd);
#else
        r = ::fsync(fd);
#endif
    } while (r < 0 && errno == EINTR);
    return r;
}
int64_t os_read(int fd, void* data, size_t size) {
    if (faults && faults->read_error) { errno = EIO; return -1; }
#ifdef _WIN32
    return _read(fd, data, unsigned(std::min(size, size_t(INT_MAX))));
#else
    return ::read(fd, data, std::min(size, size_t(SSIZE_MAX)));
#endif
}
int64_t os_write(int fd, const void* data, size_t size) {
    if (faults) {
        if (faults->interrupted_writes) { --faults->interrupted_writes; errno = EINTR; return -1; }
        if (faults->zero_write) return 0;
        if (faults->write_error) { errno = ENOSPC; return -1; }
        if (!faults->bytes_until_error && size) { errno = ENOSPC; return -1; }
        size = std::min(size, faults->bytes_until_error);
        if (faults->write_limit) size = std::min(size, faults->write_limit);
    }
#ifdef _WIN32
    int64_t result = _write(fd, data, unsigned(std::min(size, size_t(INT_MAX))));
#else
    int64_t result = ::write(fd, data, std::min(size, size_t(SSIZE_MAX)));
#endif
    if (faults && result > 0 && faults->bytes_until_error != SIZE_MAX) faults->bytes_until_error -= size_t(result);
    return result;
}
bool plain_target(const Path& path) {
    std::error_code ec;
    auto status = fs::symlink_status(path, ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec) {
        LOGE("[save-io] inspect target FAILED path=%s reason=%s", path_text(path).c_str(), ec.message().c_str());
        errno = filesystem_error(ec); return false;
    }
    if (!fs::exists(status)) return true;
    if (!fs::is_regular_file(status) || fs::is_symlink(status)) { errno = EINVAL; return false; }
#ifdef _WIN32
    DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES || (a & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY))) { errno = EACCES; return false; }
#else
    if ((status.permissions() & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) == fs::perms::none) { errno = EACCES; return false; }
#endif
    return true;
}
bool allowed_parent(const Path& path) {
    if (profile_root.empty()) return true;  // standalone settings/tests without a configured profile
    std::error_code ec;
    auto parent = fs::weakly_canonical(path.parent_path(), ec);
    if (ec) { errno = EACCES; return false; }
    for (;;) {
        if (parent == profile_root || fs::equivalent(parent, profile_root, ec)) return true;
        if (parent == parent.root_path()) { errno = EACCES; return false; }
        parent = parent.parent_path();
    }
}
int temporary_file(const Path& final, Path& path, int flags) {
    path.clear();
    for (unsigned i = 0; i < 64; i++) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        Path candidate = final.parent_path() / (".ds-save-" + std::to_string(stamp) + "-" + std::to_string(++serial) + ".tmp");
        int fd = os_open(candidate, (flags & O_ACCMODE) | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) { path = std::move(candidate); return fd; }
        if (errno != EEXIST) return fd;
    }
    errno = EEXIST;
    return -1;
}
std::shared_ptr<Entry> lookup(int fd) {
    std::lock_guard lock(registry_mutex);
    auto it = entries.find(fd);
    return it == entries.end() ? nullptr : it->second;
}
std::shared_ptr<Entry> lookup(std::FILE* stream) {
    std::lock_guard lock(registry_mutex);
    auto it = streams.find(stream);
    return it == streams.end() ? nullptr : it->second;
}
void discard(Entry& entry) {
    if (entry.temporary.empty()) return;
    std::error_code ec;
    fs::remove(entry.temporary, ec);  // only this entry's exclusively-created file
    if (ec) LOGW("[save-io] temporary cleanup failed path=%s error=%s; not a committed save", path_text(entry.temporary).c_str(), ec.message().c_str());
    entry.temporary.clear();
}
Entry::~Entry() {
    // An abandoned transaction must never publish during destruction/shutdown.
    if (!closed && fd >= 0) {
        if (stream) std::fclose(stream); else os_close(fd);
    }
    discard(*this);
}
save_validation::Result inspect_file(const Path& path, uint64_t* size_out = nullptr, bool* io_ok = nullptr) {
    save_validation::Result result;
    result.status = save_validation::Status::unknown;
    if (io_ok) *io_ok = false;
    int fd = os_open(path, O_RDONLY, 0);
    if (fd < 0) { result.reason = "open inspection failed: " + std::string(std::strerror(errno)); return result; }
    std::vector<uint8_t> data;
    std::array<uint8_t, 16384> block;
    bool okay = true, limited = false;
    for (;;) {
        int64_t n;
        do { n = os_read(fd, block.data(), block.size()); } while (n < 0 && errno == EINTR);
        if (n < 0) { result.reason = "inspection read failed: " + std::string(std::strerror(errno)); okay = false; break; }
        if (!n) break;
        if (data.size() + size_t(n) > inspect_limit) {
            result.status = save_validation::Status::unknown;
            result.reason = "inspection size limit exceeded; no compatibility claim";
            limited = true;
            okay = false;
            break;
        }
        data.insert(data.end(), block.begin(), block.begin() + n);
    }
    int close_result = os_close(fd);
    if (size_out) *size_out = data.size();
    if (close_result < 0) { result.reason = "inspection close failed"; return result; }
    if (io_ok) *io_ok = okay || limited;
    return okay ? save_validation::inspect(data) : result;
}
save_validation::Result inspect_open_read(int fd, uint64_t& size, bool& inspected) {
    save_validation::Result result;
    result.status = save_validation::Status::unknown;
    inspected = false;
#ifdef _WIN32
    struct _stat64 status{};
    if (_fstat64(fd, &status) || (status.st_mode & S_IFMT) != S_IFREG) { result.reason = "not a seekable regular profile file"; return result; }
    int64_t position = _lseeki64(fd, 0, SEEK_CUR);
#else
    struct stat status{};
    if (::fstat(fd, &status) || !S_ISREG(status.st_mode)) { result.reason = "not a seekable regular profile file"; return result; }
    int64_t position = ::lseek(fd, 0, SEEK_CUR);
#endif
    if (position != 0 || uint64_t(status.st_size) > inspect_limit) { result.reason = "inspection offset/size limit; native loader authoritative"; return result; }
    std::vector<uint8_t> bytes;
    bytes.reserve(size_t(status.st_size));
    std::array<uint8_t,16384> buffer;
    bool okay = true;
    for (;;) {
        int64_t n = os_read(fd, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { okay = false; result.reason = "profile inspection read failed: " + std::string(std::strerror(errno)); break; }
        if (!n) break;
        if (bytes.size() + size_t(n) > inspect_limit) { okay = false; result.reason = "profile grew past inspection limit"; break; }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + n);
    }
#ifdef _WIN32
    if (_lseeki64(fd, position, SEEK_SET) < 0) { okay = false; result.reason = "cannot restore profile read offset after inspection"; }
#else
    if (::lseek(fd, off_t(position), SEEK_SET) < 0) { okay = false; result.reason = "cannot restore profile read offset after inspection"; }
#endif
    size = bytes.size();
    if (!okay) return result;
    inspected = true;
    return save_validation::inspect(bytes);
}
bool replace(const Path& source, const Path& destination, bool is_backup = false) {
    if (faults && faults->replace_error && !is_backup) { errno = EACCES; return false; }
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DWORD error = GetLastError();
    errno = error == ERROR_DISK_FULL ? ENOSPC : error == ERROR_SHARING_VIOLATION ? EBUSY : EACCES;
    LOGE("[save-io] Windows replacement error=%lu destination=%s", (unsigned long)error, path_text(destination).c_str());
    return false;
#else
    return ::rename(source.c_str(), destination.c_str()) == 0;
#endif
}
bool sync_directory(const Path& parent) {
    if (faults && faults->directory_sync_error) { errno = EIO; return false; }
#ifdef _WIN32
    // MoveFileExW WRITE_THROUGH is the available publication barrier here;
    // Windows does not expose POSIX directory fsync through the CRT.
    (void)parent;
    return true;
#else
    int fd = os_open(parent, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return false;
    int r = os_sync(fd), error = errno;
    int closed = os_close(fd);
    if (r < 0) { errno = error; return false; }
    return closed == 0;
#endif
}
bool backup(Entry& entry) {
    std::error_code ec;
    if (!fs::exists(entry.path, ec)) {
        if (ec) { failure(entry, "inspect previous file", EIO); return false; }
        return true;
    }
    // Never rotate an invalid or unverified final over a previously known-good backup.
    uint64_t previous_digest = 0;
    if (!entry.text) {
        bool readable = false;
        auto previous = inspect_file(entry.path, nullptr, &readable);
        if (!readable) {
            LOGE("[save-io] previous save inspection FAILED path=%s reason=%s", path_text(entry.path).c_str(), previous.reason.c_str());
            failure(entry, "inspect previous save for backup", EIO); return false;
        }
        if (previous.status != save_validation::Status::valid) {
            LOGW("[save-io] previous final not rotated into backup path=%s reason=%s; existing backup retained", path_text(entry.path).c_str(), previous.reason.c_str());
            return true;
        }
        previous_digest = previous.digest;
    }
    Path target = entry.path; target += ".bak";
    if (!plain_target(target)) { failure(entry, "backup target", errno); return false; }
    if (faults && faults->backup_error) { failure(entry, "backup", ENOSPC); return false; }
    Entry copy;
    copy.path = target;
    copy.fd = temporary_file(target, copy.temporary, O_WRONLY);
    if (copy.fd < 0) { failure(entry, "backup temporary open", errno); return false; }
    int input = os_open(entry.path, O_RDONLY, 0);
    bool okay = input >= 0;
    int error = errno;
    std::array<uint8_t, 16384> buffer;
    while (okay) {
        int64_t n;
        do { n = os_read(input, buffer.data(), buffer.size()); } while (n < 0 && errno == EINTR);
        if (n < 0) { okay = false; error = errno; break; }
        if (!n) break;
        size_t done = 0;
        while (done < size_t(n)) {
            int64_t wrote = os_write(copy.fd, buffer.data() + done, size_t(n) - done);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) { okay = false; error = wrote < 0 ? errno : EIO; break; }
            done += size_t(wrote);
        }
    }
    if (input >= 0 && os_close(input) < 0) { okay = false; error = errno; }
    if (okay && os_sync(copy.fd) < 0) { okay = false; error = errno; }
    if (os_close(copy.fd) < 0) { okay = false; error = errno; }
    copy.closed = true;
    if (okay && !entry.text) {
        bool readable = false;
        auto snapshot = inspect_file(copy.temporary, nullptr, &readable);
        if (!readable || snapshot.status != save_validation::Status::valid || snapshot.digest != previous_digest) {
            LOGE("[save-io] backup snapshot changed/invalid path=%s reason=%s; existing backup retained", path_text(entry.path).c_str(), snapshot.reason.c_str());
            okay = false; error = EIO;
        }
    }
    if (okay && !replace(copy.temporary, target, true)) { okay = false; error = errno; }
    if (okay) copy.temporary.clear();
    if (okay && !sync_directory(target.parent_path())) { okay = false; error = errno; }
    if (okay) stage(Stage::backup_ready);
    if (!okay) failure(entry, "backup publication", error);
    return okay;
}
bool publish(Entry& entry) {
    if (entry.error) { discard(entry); errno = entry.error; return false; }
    uint64_t size = 0;
    if (!entry.text) {
        bool readable = false;
        auto result = inspect_file(entry.temporary, &size, &readable);
        LOGI("[save-io] serialized path=%s bytes=%llu fnv64=%016llx status=%s reason=%s",
             path_text(entry.path).c_str(), (unsigned long long)size, (unsigned long long)result.digest,
             save_validation::name(result.status), result.reason.c_str());
        if (!readable) { failure(entry, "inspect staged save I/O", EIO); discard(entry); return false; }
        if (result.status == save_validation::Status::invalid) { failure(entry, "validate staged save", EIO); discard(entry); return false; }
    }
    if (!plain_target(entry.path) || !allowed_parent(entry.path)) { failure(entry, "final target changed", errno); discard(entry); return false; }
    if (!backup(entry)) { discard(entry); return false; }
    if (!replace(entry.temporary, entry.path)) { failure(entry, "atomic replace (old final retained)", errno); discard(entry); return false; }
    entry.temporary.clear();  // publication happened; never claim rollback after this point
    stage(Stage::final_replaced);
    if (!sync_directory(entry.path.parent_path())) {
        failure(entry, "directory durability after publication (new final installed; durability uncertain)", errno);
        return false;
    }
    LOGI("[save-io] COMMITTED path=%s written=%llu backup=previous-validated-file-if-available", path_text(entry.path).c_str(), (unsigned long long)entry.written);
    return true;
}
bool finish_closed(const std::shared_ptr<Entry>& entry) {
    if (!entry->transaction) return !entry->error;
    if (entry->deferred) {
        if (auto scope = entry->owner.lock()) {
            if (entry->error) scope->failed = true;
        } else if (!entry->error) failure(*entry, "serialization scope ended before close", EIO);
        if (entry->error) discard(*entry);
        return !entry->error;
    }
    return publish(*entry);
}
int open_impl(const Path& requested, int flags, int mode, bool text = false) {
    auto entry = std::make_shared<Entry>();
    std::error_code ec;
    entry->path = fs::absolute(requested, ec).lexically_normal();
    if (ec) { errno = EINVAL; return -1; }
    entry->text = text;
    entry->tagged = text || profile_file(entry->path);
    entry->reading = (flags & O_ACCMODE) != O_WRONLY;
    entry->transaction = entry->tagged && (flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY;
    if (entry->tagged && (flags & O_ACCMODE) != O_RDONLY) {
        std::lock_guard lock(registry_mutex);
        auto it = writers.find(entry->path);
        if (it != writers.end() && !it->second.expired()) { failure(*entry, "concurrent writer", EBUSY); return -1; }
        writers[entry->path] = entry;
    }
    if (entry->transaction) {
        if (!text && !scopes.empty()) {
            entry->owner = scopes.back();
            entry->deferred = true;
            scopes.back()->opened.push_back(entry);
        }
        if (!allowed_parent(entry->path) || !plain_target(entry->path)) { failure(*entry, "save target", errno); return -1; }
        fs::create_directories(entry->path.parent_path(), ec);
        if (ec) {
            LOGE("[save-io] save directory creation FAILED path=%s reason=%s", path_text(entry->path.parent_path()).c_str(), ec.message().c_str());
            failure(*entry, "create save directory", filesystem_error(ec)); return -1;
        }
        if ((flags & O_EXCL) && fs::exists(entry->path, ec)) { failure(*entry, "exclusive save open", EEXIST); return -1; }
        entry->fd = temporary_file(entry->path, entry->temporary, flags);
    } else entry->fd = os_open(entry->path, flags, mode);
    if (entry->fd < 0) {
        int error = errno;
        if (entry->tagged && error == ENOENT && !entry->transaction)
            LOGI("[save-io] read open absent path=%s (may be first run/unused slot; no recovery or deletion)", path_text(entry->path).c_str());
        else if (entry->tagged) failure(*entry, "open", error);
        errno = error;
        return -1;
    }
    {
        std::lock_guard lock(registry_mutex);
        entries[entry->fd] = entry;
    }
    if (entry->tagged) {
        LOGI("[save-io] %s open path=%s fd=%d%s", entry->transaction ? "staged write" : "read/direct", path_text(entry->path).c_str(), entry->fd,
             entry->transaction ? " final-unchanged-until-commit" : "");
        if (!entry->transaction && (flags & O_ACCMODE) != O_RDONLY)
            LOGW("[save-io] non-truncating profile write retains normal POSIX semantics path=%s; not an atomic replacement", path_text(entry->path).c_str());
        if (entry->reading && !entry->transaction && !entry->text) {
            uint64_t size = 0;
            bool inspected = false;
            auto result = inspect_open_read(entry->fd, size, inspected);
            entry->load_invalid = inspected && result.status == save_validation::Status::invalid;
            LOGI("[save-load] selected path=%s bytes=%llu fnv64=%016llx status=%s reason=%s; native loader remains authoritative",
                 path_text(entry->path).c_str(), (unsigned long long)size, (unsigned long long)result.digest,
                 save_validation::name(result.status), result.reason.c_str());
            loads.emplace_back(entry->fd, path_text(entry->path));
        }
    }
    return entry->fd;
}
void unregister(const std::shared_ptr<Entry>& entry) {
    std::lock_guard lock(registry_mutex);
    entries.erase(entry->fd);
    if (entry->stream) streams.erase(entry->stream);
    std::erase_if(loads, [&](const auto& item) { return item.first == entry->fd; });
}
}

void test_faults(Faults* injected) { faults = injected; }
void set_profile_root(const Path& root) {
    std::error_code ec;
    profile_root = root.empty() ? Path{} : fs::weakly_canonical(root, ec);
    if (ec) profile_root = fs::absolute(root).lexically_normal();
}
int open(const Path& path, int flags, int mode) { return open_impl(path, flags, mode); }
int close(int fd) {
    auto entry = lookup(fd);
    if (!entry) return os_close(fd);
    std::lock_guard lock(entry->mutex);
    if (entry->closed || entry->stream) { errno = EBADF; return -1; }
    bool okay = true;
    if (entry->transaction && !entry->error) {
        if (os_sync(fd) < 0) { failure(*entry, "sync before close", errno); okay = false; }
        else stage(Stage::data_synced);
    }
    unregister(entry);
    if (os_close(fd) < 0) { failure(*entry, "close", errno); okay = false; }
    entry->closed = true;
    if (entry->transaction && okay && !entry->error) stage(Stage::stream_closed);
    if (!okay && !entry->error) entry->error = EIO;
    if (entry->tagged && entry->reading && !entry->transaction)
        LOGI("[save-load] file closed path=%s read=%llu status=%s (serialization/restoration reported separately)", path_text(entry->path).c_str(), (unsigned long long)entry->read, okay ? "ok" : "FAILED");
    okay = finish_closed(entry) && okay;
    if (!okay) { errno = entry->error ? entry->error : EIO; return -1; }
    return 0;
}
int64_t read(int fd, void* data, size_t size) {
    auto entry = lookup(fd);
    if (!entry) return os_read(fd, data, size);
    std::lock_guard lock(entry->mutex);
    if (entry->closed) { errno = EBADF; return -1; }
    int64_t result;
    do { result = os_read(fd, data, size); } while (result < 0 && errno == EINTR);
    if (result < 0 && entry->tagged) failure(*entry, "read", errno, size);
    if (result >= 0) entry->read += uint64_t(result);
    if (result >= 0 && uint64_t(result) < size && entry->tagged)
        LOGI("[save-load] short read/EOF path=%s requested=%llu got=%lld; EOF alone is not corruption", path_text(entry->path).c_str(), (unsigned long long)size, (long long)result);
    return result;
}
int64_t write(int fd, const void* data, size_t size) {
    auto entry = lookup(fd);
    if (!entry) return os_write(fd, data, size);
    std::lock_guard lock(entry->mutex);
    if (entry->closed) { errno = EBADF; return -1; }
    if (entry->error) { errno = entry->error; return -1; }
    if (!size) return 0;
    size_t done = 0;
    do {
        int64_t n = os_write(fd, static_cast<const uint8_t*>(data) + done, size - done);
        if (n < 0 && errno == EINTR && entry->transaction) continue;
        if (n < 0 || (!n && size > done && entry->transaction)) {
            int error = n < 0 ? errno : EIO;
            if (entry->tagged) failure(*entry, "write", error, size, done);
            errno = error;
            return -1;
        }
        done += size_t(n);
        entry->written += uint64_t(n);
        if (!entry->transaction) return n;  // do not impose write-all on generic POSIX streams
    } while (done < size);
    return int64_t(done);
}
int64_t seek(int fd, int64_t offset, int origin) {
    auto entry = lookup(fd);
    std::unique_lock<std::mutex> lock;
    if (entry) { lock = std::unique_lock(entry->mutex); if (entry->closed) { errno = EBADF; return -1; } }
#ifdef _WIN32
    int64_t result = _lseeki64(fd, offset, origin);
#else
    int64_t result = ::lseek(fd, off_t(offset), origin);
#endif
    if (result < 0 && entry && entry->tagged) failure(*entry, "seek", errno);
    return result;
}
int sync(int fd) {
    auto entry = lookup(fd);
    std::unique_lock<std::mutex> lock;
    if (entry) { lock = std::unique_lock(entry->mutex); if (entry->closed) { errno = EBADF; return -1; } }
    int result = os_sync(fd);
    if (result < 0 && entry && entry->tagged) failure(*entry, "fsync", errno);
    return result;
}
int truncate(int fd, uint64_t size) {
    auto entry = lookup(fd);
    std::unique_lock<std::mutex> lock;
    if (entry) { lock = std::unique_lock(entry->mutex); if (entry->closed) { errno = EBADF; return -1; } }
    if (size > uint64_t(INT64_MAX)) { errno = EINVAL; return -1; }
#ifdef _WIN32
    int error = _chsize_s(fd, size);
    int result = error ? -1 : 0;
    if (error) errno = error;
#else
    int result = ::ftruncate(fd, off_t(size));
#endif
    if (result < 0 && entry && entry->tagged) failure(*entry, "truncate", errno);
    return result;
}
void begin_save() { scopes.push_back(std::make_shared<Scope>()); }
bool finish_save(bool serialized) {
    if (scopes.empty()) return serialized;
    auto scope = std::move(scopes.back());
    scopes.pop_back();
    if (scope->opened.empty()) LOGI("[save-io] serialization scope had no managed profile output; native result retained, no atomic publication claimed");
    bool okay = serialized && !scope->failed;
    for (auto& entry : scope->opened) {
        std::lock_guard lock(entry->mutex);
        if (!entry->closed || entry->fd < 0 || entry->error) {
            if (!entry->error) failure(*entry, "save scope incomplete/open failed", EIO);
            okay = false;
        }
    }
    if (!okay) {
        for (auto& entry : scope->opened) {
            std::lock_guard lock(entry->mutex);
            if (!entry->error) entry->error = EIO;
            if (entry->closed) discard(*entry);
        }
        if (!scopes.empty()) scopes.back()->failed = true;
        LOGE("[save-io] save operation FAILED serialization=%s io=%s; staged output discarded or marked failed, prior final retained", serialized ? "ok" : "FAILED", scope->failed ? "FAILED" : "ok");
        return false;
    }
    for (auto& entry : scope->opened) { std::lock_guard lock(entry->mutex); okay = publish(*entry) && okay; }
    if (!okay && !scopes.empty()) scopes.back()->failed = true;
    return okay;
}
std::string current_load() { return loads.empty() ? std::string{} : loads.back().second; }
LoadInfo load_info(int fd) {
    auto entry = lookup(fd);
    if (!entry) return {};
    std::lock_guard lock(entry->mutex);
    if (entry->closed || !entry->tagged || !entry->reading || entry->transaction || entry->text) return {};
    return {path_text(entry->path), entry->load_invalid};
}
bool tracked_profile_fd(int fd) { auto entry = lookup(fd); return entry && entry->tagged; }
bool failed_fd(int fd) { auto entry = lookup(fd); if (!entry) return false; std::lock_guard lock(entry->mutex); return entry->error != 0; }
uint64_t failure_epoch() { return failures; }

std::FILE* fopen(const Path& path, const std::string& mode) {
    if (mode.empty() || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) { errno = EINVAL; return nullptr; }
    int flags = mode.find('+') != std::string::npos ? O_RDWR : mode[0] == 'r' ? O_RDONLY : O_WRONLY;
    if (mode[0] == 'w') flags |= O_CREAT | O_TRUNC;
    if (mode[0] == 'a') flags |= O_CREAT | O_APPEND;
    if (mode.find('x') != std::string::npos) flags |= O_EXCL;
    int fd = save_io::open(path, flags);
    if (fd < 0) return nullptr;
    auto entry = lookup(fd);
    std::string binary = mode;
    if (binary.find('b') == std::string::npos) binary += 'b';
#ifdef _WIN32
    std::FILE* stream = _fdopen(fd, binary.c_str());
#else
    std::FILE* stream = ::fdopen(fd, binary.c_str());
#endif
    if (!stream) {
        int error = errno;
        { std::lock_guard lock(entry->mutex); failure(*entry, "fdopen", error); }
        save_io::close(fd);
        errno = error;
        return nullptr;
    }
    entry->stream = stream;
    std::lock_guard lock(registry_mutex);
    streams[stream] = entry;
    return stream;
}
int fclose(std::FILE* stream) {
    auto entry = lookup(stream);
    if (!entry) return std::fclose(stream);
    std::lock_guard lock(entry->mutex);
    if (entry->closed) { errno = EBADF; return EOF; }
    bool okay = true;
    if (std::ferror(stream)) { failure(*entry, "stdio error before close", EIO); okay = false; }
    if (entry->transaction) {
        int flushed = std::fflush(stream);
        if (faults && faults->flush_error) { errno = EIO; flushed = EOF; }
        if (flushed != 0) { failure(*entry, "fflush before close", errno); okay = false; }
        if (os_sync(entry->fd) != 0) { failure(*entry, "stdio sync before close", errno); okay = false; }
        else if (okay && !entry->error) stage(Stage::data_synced);
    }
    unregister(entry);
    if (std::fclose(stream) != 0) { failure(*entry, "fclose", errno); okay = false; }
    if (faults && faults->close_error) { failure(*entry, "fclose", EIO); okay = false; }
    entry->closed = true;
    if (entry->transaction && okay && !entry->error) stage(Stage::stream_closed);
    if (!okay && !entry->error) entry->error = EIO;
    okay = finish_closed(entry) && okay;
    if (!okay) { errno = entry->error ? entry->error : EIO; return EOF; }
    return 0;
}
size_t fread(void* data, size_t size, size_t count, std::FILE* stream) {
    if (size && count > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }
    auto entry = lookup(stream);
    std::unique_lock<std::mutex> lock;
    if (entry) { lock = std::unique_lock(entry->mutex); if (entry->closed) { errno = EBADF; return 0; } }
    size_t result = std::fread(data, size, count, stream);
    if (entry && entry->tagged) {
        entry->read += result * size;
        if (std::ferror(stream)) failure(*entry, "fread", errno, uint64_t(size) * count, result * size);
    }
    return result;
}
size_t fwrite(const void* data, size_t size, size_t count, std::FILE* stream) {
    auto entry = lookup(stream);
    if (size && count > SIZE_MAX / size) {
        if (entry) { std::lock_guard lock(entry->mutex); failure(*entry, "fwrite size overflow", EOVERFLOW); }
        else errno = EOVERFLOW;
        return 0;
    }
    if (!size || !count) return 0;
    if (!entry) return std::fwrite(data, size, count, stream);
    std::lock_guard lock(entry->mutex);
    if (entry->closed || entry->error) {
        failure(*entry, "fwrite on failed/closed stream", entry->error ? entry->error : EBADF);
        return 0;
    }
    if (!entry->transaction) {
        size_t result = std::fwrite(data, size, count, stream);
        if (std::ferror(stream) && entry->tagged) failure(*entry, "fwrite", errno, size * count, result * size);
        return result;
    }
    size_t total = size * count, done = 0;
    while (done < total) {
        size_t n = 0;
        if (faults && faults->interrupted_writes) { --faults->interrupted_writes; continue; }
        if (faults && (faults->write_error || faults->zero_write || !faults->bytes_until_error)) errno = faults->zero_write ? EIO : ENOSPC;
        else {
            size_t request = faults && faults->write_limit ? std::min(total - done, faults->write_limit) : total - done;
            if (faults) request = std::min(request, faults->bytes_until_error);
            n = std::fwrite(static_cast<const uint8_t*>(data) + done, 1, request, stream);
            if (faults && faults->bytes_until_error != SIZE_MAX) faults->bytes_until_error -= n;
        }
        if (!n) {
            if (std::ferror(stream) && errno == EINTR) { std::clearerr(stream); continue; }
            failure(*entry, "fwrite", errno ? errno : EIO, total, done);
            break;
        }
        done += n;
        entry->written += n;
    }
    return done / size;
}
int fflush(std::FILE* stream) {
    if (!stream) {
        std::vector<std::shared_ptr<Entry>> all;
        { std::lock_guard lock(registry_mutex); for (const auto& [file, entry] : streams) all.push_back(entry); }
        int result = std::fflush(nullptr);
        int error = result ? errno : 0;
        for (auto& entry : all) {
            std::lock_guard lock(entry->mutex);
            if (entry->closed) continue;
            if (std::fflush(entry->stream) != 0 || std::ferror(entry->stream)) {
                failure(*entry, "fflush all", errno ? errno : EIO);
                error = errno; result = EOF;
            }
        }
        if (error) errno = error;
        return result;
    }
    auto entry = lookup(stream);
    std::unique_lock<std::mutex> lock;
    if (entry) { lock = std::unique_lock(entry->mutex); if (entry->closed) { errno = EBADF; return EOF; } }
    int result = std::fflush(stream);
    if (faults && faults->flush_error) { errno = EIO; result = EOF; }
    if (result != 0 && entry) failure(*entry, "fflush", errno);
    return result;
}
void stream_error(std::FILE* stream, const char* operation) {
    auto entry = lookup(stream);
    if (entry && std::ferror(stream)) { std::lock_guard lock(entry->mutex); failure(*entry, operation, errno ? errno : EIO); }
}
void fail_stream(std::FILE* stream, const char* operation, int error) {
    auto entry = lookup(stream);
    if (entry) { std::lock_guard lock(entry->mutex); failure(*entry, operation, error); }
    errno = error;
}
void fail_fd(int fd, const char* operation, int error) {
    auto entry = lookup(fd);
    if (entry) { std::lock_guard lock(entry->mutex); failure(*entry, operation, error); }
    errno = error;
}
bool stream_failed(std::FILE* stream) {
    auto entry = lookup(stream);
    if (!entry) return std::ferror(stream) != 0;
    std::lock_guard lock(entry->mutex);
    return entry->stream_error || (!entry->closed && std::ferror(stream));
}
void clear_stream_error(std::FILE* stream) {
    auto entry = lookup(stream);
    if (!entry) { std::clearerr(stream); return; }
    std::lock_guard lock(entry->mutex);
    if (!entry->closed) std::clearerr(stream);
    entry->stream_error = false;  // a failed transaction itself remains unpublishable
}
bool atomic_text(const Path& path, std::string_view text) {
    int fd = open_impl(path, O_WRONLY | O_CREAT | O_TRUNC, 0644, true);
    if (fd < 0) return false;
    bool okay = save_io::write(fd, text.data(), text.size()) == int64_t(text.size());
    return save_io::close(fd) == 0 && okay;
}
void describe_library(const Path& path) {
    int fd = os_open(path, O_RDONLY, 0);
    if (fd < 0) { LOGW("[save-load] cannot fingerprint library path=%s errno=%d", path_text(path).c_str(), errno); return; }
    uint64_t size = 0, digest = 14695981039346656037ull;
    std::array<uint8_t,16384> buffer;
    bool okay = true;
    for (;;) {
        int64_t n = os_read(fd, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { okay = false; break; }
        if (!n) break;
        size += uint64_t(n);
        for (int64_t i = 0; i < n; i++) digest = (digest ^ buffer[size_t(i)]) * 1099511628211ull;
    }
    if (os_close(fd) != 0) okay = false;
    LOGI("[save-load] library path=%s bytes=%llu fnv64=%016llx identity-read=%s", path_text(path).c_str(), (unsigned long long)size, (unsigned long long)digest, okay ? "ok" : "FAILED");
}

ProfileLock::~ProfileLock() { release(); }
bool ProfileLock::acquire(const Path& root) {
    if (handle_ != -1) { errno = EBUSY; return false; }
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) { LOGE("[save-io] cannot create profile directory path=%s reason=%s", path_text(root).c_str(), ec.message().c_str()); return false; }
    Path path = root / ".deadspace-profile.lock";
    if (!plain_target(path)) { LOGE("[save-io] unsafe/inaccessible profile lock path=%s errno=%d", path_text(path).c_str(), errno); return false; }
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { LOGE("[save-io] profile lock open failed path=%s Windows-error=%lu", path_text(path).c_str(), (unsigned long)GetLastError()); return false; }
    OVERLAPPED overlapped{};
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlapped)) {
        DWORD error = GetLastError(); CloseHandle(handle);
        LOGE("[save-io] profile already in use or cannot lock path=%s Windows-error=%lu; close other instance or use a separate disposable profile", path_text(path).c_str(), (unsigned long)error);
        return false;
    }
    handle_ = reinterpret_cast<intptr_t>(handle);
#else
    int fd = os_open(path, O_RDWR | O_CREAT, 0600);
    if (fd < 0 || ::flock(fd, LOCK_EX | LOCK_NB) < 0) {
        int error = errno; if (fd >= 0) ::close(fd);
        LOGE("[save-io] profile already in use or cannot lock path=%s errno=%d (%s)", path_text(path).c_str(), error, std::strerror(error));
        return false;
    }
    handle_ = fd;
#endif
    LOGI("[save-io] exclusive profile writer acquired path=%s", path_text(root).c_str());
    // Leave stale temporary files alone; never infer that an incomplete file is a save.
    unsigned shown = 0;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        auto name = it->path().filename().string();
        if (name.starts_with(".ds-save-") && name.ends_with(".tmp") && shown++ < 8)
            LOGW("[save-io] abandoned/uncommitted temporary retained path=%s; not loaded automatically", path_text(it->path()).c_str());
    }
    if (shown > 8) LOGW("[save-io] %u uncommitted temporary files found; first 8 listed; none promoted/deleted", shown);
    if (ec) LOGW("[save-io] temporary-file inventory incomplete: %s", ec.message().c_str());
    return true;
}
void ProfileLock::release() {
    if (handle_ == -1) return;
#ifdef _WIN32
    OVERLAPPED overlapped{};
    if (!UnlockFileEx(reinterpret_cast<HANDLE>(handle_), 0, 1, 0, &overlapped))
        LOGW("[save-io] profile unlock failed Windows-error=%lu", (unsigned long)GetLastError());
    if (!CloseHandle(reinterpret_cast<HANDLE>(handle_))) LOGW("[save-io] profile lock handle close failed Windows-error=%lu", (unsigned long)GetLastError());
#else
    if (::flock(int(handle_), LOCK_UN)) LOGW("[save-io] profile unlock failed errno=%d", errno);
    if (::close(int(handle_))) LOGW("[save-io] profile lock close failed errno=%d", errno);
#endif
    handle_ = -1;
    // Do not unlink the lock file: another instance could already hold its inode.
}
}  // namespace save_io
