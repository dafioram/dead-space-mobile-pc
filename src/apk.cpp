#include "apk.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/syscall.h>
#endif

#include "log.h"
#include "sha256.h"

namespace apk {
namespace fs = std::filesystem;
namespace {

constexpr uint64_t max_member = 128ull * 1024 * 1024;
constexpr uint64_t max_assets = 1024ull * 1024 * 1024;
constexpr uint64_t max_directory = 32ull * 1024 * 1024;

struct Entry {
    std::string name;
    uint16_t method = 0, flags = 0;
    uint32_t crc = 0, csize = 0, usize = 0, local_off = 0, attributes = 0;
};

struct Archive {
    std::ifstream file;
    uint64_t size = 0, directory = 0;
    std::vector<Entry> entries;
    std::set<uint32_t> local_offsets;
};

class ImportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool okay, const std::string& why) {
    if (!okay) throw ImportError(why);
}

fs::path path(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string text(const fs::path& p) {
    auto u = p.u8string();
    return {reinterpret_cast<const char*>(u.data()), u.size()};
}

std::string clean(std::string s) {
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 32 || c == 127) c = '?';
    return s;
}

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(rd16(p)) | uint32_t(rd16(p + 2)) << 16; }

bool read_at(Archive& a, uint64_t offset, void* buffer, size_t n) {
    if (offset > a.size || n > a.size - offset) return false;
    if (!n) return true;
    a.file.clear();
    a.file.seekg(std::streamoff(offset));
    if (!a.file) return false;
    a.file.read(static_cast<char*>(buffer), std::streamsize(n));
    return size_t(a.file.gcount()) == n;
}

std::string folded(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

bool safe_name(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos) return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || std::strchr(":*?\"<>|", c)) return false;
    size_t start = 0;
    while (start < name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        auto stem = folded(part.substr(0, part.find('.')));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'))
            return false;
        start = end + 1;
    }
    return true;
}

void list_entries(Archive& a) {
    a.file.seekg(0, std::ios::end);
    auto size = a.file.tellg();
    require(size >= 22, "not a ZIP/APK (missing end record)");
    a.size = uint64_t(size);
    size_t tail = size_t(std::min<uint64_t>(a.size, 65535 + 22));
    std::vector<uint8_t> bytes(tail);
    require(read_at(a, a.size - tail, bytes.data(), tail), "cannot read ZIP end record");
    size_t end = SIZE_MAX;
    for (size_t i = tail - 22;; i--) {
        if (rd32(bytes.data() + i) == 0x06054b50 && size_t(rd16(bytes.data() + i + 20)) + 22 == tail - i) {
            end = i;
            break;
        }
        if (!i) break;
    }
    require(end != SIZE_MAX, "missing/truncated ZIP end record");
    const auto* e = bytes.data() + end;
    require(!rd16(e + 4) && !rd16(e + 6) && rd16(e + 8) == rd16(e + 10), "multi-disk ZIP is unsupported");
    uint32_t count = rd16(e + 10), directory_size = rd32(e + 12), directory = rd32(e + 16);
    require(count != 65535 && directory_size != UINT32_MAX && directory != UINT32_MAX, "ZIP64 import is not supported");
    require(directory_size <= max_directory && uint64_t(directory) + directory_size <= a.size - tail + end,
            "invalid/oversized ZIP directory");
    a.directory = directory;
    std::vector<uint8_t> central(directory_size);
    require(read_at(a, directory, central.data(), central.size()), "cannot read ZIP directory");
    size_t at = 0;
    std::set<std::string> names;
    for (uint32_t i = 0; i < count; i++) {
        require(at <= central.size() && central.size() - at >= 46, "truncated ZIP entry");
        const auto* p = central.data() + at;
        require(rd32(p) == 0x02014b50, "invalid ZIP entry signature");
        size_t n = rd16(p + 28), extra = rd16(p + 30), comment = rd16(p + 32);
        require(n && 46 + n + extra + comment <= central.size() - at, "truncated ZIP entry name/metadata");
        Entry entry;
        entry.name.assign(reinterpret_cast<const char*>(p + 46), n);
        require(safe_name(entry.name), "unsafe ZIP member pathname: " + clean(entry.name));
        std::string key = folded(entry.name);
        if (key.back() == '/') key.pop_back();
        require(names.insert(key).second, "duplicate/case-aliased ZIP member: " + clean(entry.name));
        entry.flags = rd16(p + 8);
        entry.method = rd16(p + 10);
        entry.crc = rd32(p + 16);
        entry.csize = rd32(p + 20);
        entry.usize = rd32(p + 24);
        entry.attributes = rd32(p + 38);
        entry.local_off = rd32(p + 42);
        require(!rd16(p + 34), "multi-disk ZIP member is unsupported");
        require(entry.csize != UINT32_MAX && entry.usize != UINT32_MAX && entry.local_off != UINT32_MAX, "ZIP64 member is unsupported");
        require(entry.local_off < directory && a.local_offsets.insert(entry.local_off).second,
                "invalid/aliased local header offset: " + entry.name);
        a.entries.push_back(std::move(entry));
        at += 46 + n + extra + comment;
    }
    require(at == central.size(), "ZIP directory size/count mismatch");
}

std::vector<uint8_t> extract(Archive& a, const Entry& e) {
    require(!(e.flags & 1), "encrypted member is unsupported: " + e.name);
    require(e.method == 0 || e.method == 8, "unsupported compression for " + e.name);
    require(e.csize <= max_member && e.usize <= max_member, "member exceeds 128 MiB import limit: " + e.name);
    require(((e.attributes >> 16) & 0170000) != 0120000, "symlink member is not a game asset: " + e.name);
    uint8_t h[30];
    require(read_at(a, e.local_off, h, sizeof h) && rd32(h) == 0x04034b50, "invalid local header: " + e.name);
    require(rd16(h + 6) == e.flags && rd16(h + 8) == e.method, "local/central flags or compression mismatch: " + e.name);
    uint64_t offset = uint64_t(e.local_off) + 30 + rd16(h + 26) + rd16(h + 28);
    auto next = a.local_offsets.upper_bound(e.local_off);
    uint64_t limit = next == a.local_offsets.end() ? a.directory : *next;
    require(offset <= limit && e.csize <= limit - offset, "member overlaps the next local header/ZIP directory: " + e.name);
    std::string local_name(rd16(h + 26), '\0');
    require(read_at(a, uint64_t(e.local_off) + 30, local_name.data(), local_name.size()) && local_name == e.name,
            "local/central name mismatch: " + e.name);
    if (!(e.flags & 8))
        require(rd32(h + 14) == e.crc && rd32(h + 18) == e.csize && rd32(h + 22) == e.usize,
                "local/central size or CRC mismatch: " + e.name);
    std::vector<uint8_t> compressed(e.csize);
    require(read_at(a, offset, compressed.data(), compressed.size()), "cannot read compressed member: " + e.name);
    std::vector<uint8_t> out;
    if (e.method == 0) {
        require(e.csize == e.usize, "stored member size mismatch: " + e.name);
        out = std::move(compressed);
    } else {
        out.resize(std::max(uint32_t(1), e.usize));
        z_stream z{};
        require(inflateInit2(&z, -MAX_WBITS) == Z_OK, "cannot initialize ZIP inflater");
        z.next_in = compressed.data();
        z.avail_in = uInt(compressed.size());
        z.next_out = out.data();
        z.avail_out = uInt(out.size());
        int result = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        require(result == Z_STREAM_END && z.total_out == e.usize && z.total_in == e.csize, "invalid/truncated DEFLATE member: " + e.name);
        out.resize(e.usize);
    }
    uint32_t actual = uint32_t(crc32(0, out.data(), uInt(out.size())));
    require(actual == e.crc, "CRC mismatch: " + e.name);
    return out;
}

bool arm32(const std::vector<uint8_t>& bytes) {
    constexpr uint8_t magic[] = {0x7f, 'E', 'L', 'F'};
    return bytes.size() >= 52 && !std::memcmp(bytes.data(), magic, sizeof magic) &&
           bytes[4] == 1 && bytes[5] == 1 && bytes[6] == 1 && rd16(bytes.data() + 16) == 3 &&
           rd16(bytes.data() + 18) == 40 && rd32(bytes.data() + 20) == 1 && rd16(bytes.data() + 40) == 52;
}

bool plain(const fs::path& p) {
    std::error_code ec;
    auto status = fs::symlink_status(p, ec);
    if (ec == std::errc::no_such_file_or_directory) return true;
    if (ec || fs::is_symlink(status)) return false;
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
    return true;
}

struct Stage {
    fs::path root;
    ~Stage() {
        if (root.empty()) return;
        std::error_code ec;
        fs::remove_all(root, ec);  // only this import's exclusively-created staging root
        if (ec) LOGW("[apk-import] owned stage cleanup failed path=%s reason=%s", clean(text(root)).c_str(), ec.message().c_str());
    }
};

fs::path new_stage(const fs::path& parent) {
    static std::atomic<unsigned long long> sequence{0};
    for (unsigned i = 0; i < 64; i++) {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        auto p = parent / (".deadspace-import-" + std::to_string(stamp) + "-" + std::to_string(++sequence));
        std::error_code ec;
        if (fs::create_directory(p, ec)) return p;
        require(!ec || ec == std::errc::file_exists, "cannot create import stage: " + ec.message());
    }
    throw ImportError("cannot reserve a unique import stage");
}

void write_new(const fs::path& p, std::span<const uint8_t> bytes, ImportFaults* faults) {
    fs::create_directories(p.parent_path());
#ifdef _WIN32
    int fd = _wopen(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
    std::FILE* f = fd >= 0 ? _fdopen(fd, "wb") : nullptr;
#else
    int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    std::FILE* f = fd >= 0 ? fdopen(fd, "wb") : nullptr;
#endif
    if (!f && fd >= 0) {
#ifdef _WIN32
        _close(fd);
#else
        ::close(fd);
#endif
    }
    require(f != nullptr, "cannot create staged file exclusively: " + clean(text(p)) + " (" + std::strerror(errno) + ")");
    size_t done = 0;
    bool okay = true;
    int error = 0;
    while (done < bytes.size()) {
        size_t n = std::min(bytes.size() - done, size_t(65536));
        if (faults) n = std::min(n, faults->bytes_before_error);
        if (!n) {
            okay = false;
            error = ENOSPC;
            break;
        }
        errno = 0;
        size_t written = std::fwrite(bytes.data() + done, 1, n, f);
        done += written;
        if (faults) faults->bytes_before_error -= written;
        if (written < n && std::ferror(f)) {
            if (errno == EINTR) {
                std::clearerr(f);
                continue;
            }
            okay = false;
            error = errno ? errno : EIO;
            break;
        }
        if (!written) {
            okay = false;
            error = EIO;
            break;
        }
    }
    if (std::fflush(f) != 0) {
        okay = false;
        error = errno;
    }
    if (std::fclose(f) != 0) {
        okay = false;
        error = errno;
    }
    if (faults && faults->close_error) {
        okay = false;
        error = EIO;
    }
    require(okay, "staged write/flush/close failed: " + clean(text(p)) + " requested=" + std::to_string(bytes.size()) +
                  " completed=" + std::to_string(done) + " errno=" + std::to_string(error) + " (" + std::strerror(error) + ")");
}

void write_new(const fs::path& p, const std::string& text, ImportFaults* faults) {
    write_new(p, std::span(reinterpret_cast<const uint8_t*>(text.data()), text.size()), faults);
}

bool same_file(const fs::path& a, const fs::path& b) {
    if (!plain(a) || !plain(b) || !fs::is_regular_file(a) || !fs::is_regular_file(b) || fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    if (!x || !y) return false;
    std::array<char, 65536> bx, by;
    while (x) {
        x.read(bx.data(), bx.size());
        y.read(by.data(), by.size());
        if (x.gcount() != y.gcount() || std::memcmp(bx.data(), by.data(), size_t(x.gcount()))) return false;
    }
    return x.eof() && !x.bad() && !y.bad();
}

bool same_tree(const fs::path& a, const fs::path& b) {
    if (!plain(a) || !plain(b) || !fs::is_directory(a) || !fs::is_directory(b)) return false;
    std::set<fs::path> expected, actual;
    for (const auto& item : fs::recursive_directory_iterator(a)) {
        if (!plain(item.path())) return false;
        if (item.is_regular_file()) expected.insert(item.path().lexically_relative(a));
        else if (!item.is_directory()) return false;
    }
    for (const auto& item : fs::recursive_directory_iterator(b)) {
        if (!plain(item.path())) return false;
        if (item.is_regular_file()) actual.insert(item.path().lexically_relative(b));
        else if (!item.is_directory()) return false;
    }
    if (expected != actual) return false;
    for (const auto& file : expected)
        if (!same_file(a / file, b / file)) return false;
    return true;
}

void publish(const fs::path& stage, const fs::path& dest) {
#ifdef _WIN32
    if (MoveFileExW(stage.c_str(), dest.c_str(), MOVEFILE_WRITE_THROUGH)) return;
    throw ImportError("directory publication failed without replacement: " + clean(text(dest)) +
                      " Windows-error=" + std::to_string(GetLastError()));
#elif defined(__linux__) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, stage.c_str(), AT_FDCWD, dest.c_str(), 1 /*RENAME_NOREPLACE*/) == 0) return;
    int error = errno;
    throw ImportError("directory publication failed without replacement: " + clean(text(dest)) + " (" + std::strerror(error) + ")");
#else
    // Fail rather than silently using a rename that could replace an existing tree.
    throw ImportError("no no-replace directory publication API on this platform");
#endif
}

}  // namespace

bool import(const std::string& apk_path, const std::string& destination, ImportFaults* faults) {
    try {
        require(!apk_path.empty() && !destination.empty(), "APK source and game-data destination are required");
        Archive archive;
        archive.file.open(path(apk_path), std::ios::binary);
        require(bool(archive.file), "cannot open APK: " + clean(apk_path));
        list_entries(archive);
        for (const auto& e : archive.entries)
            if (e.name == "assets/game/libDeadSpace32.so")
                throw ImportError("Reanimated-style nested ARM32/HD package detected. This release does not import Reanimated/HD yet; "
                                  "use an original-layout 32-bit APK. No files extracted; destination unchanged.");

        const Entry* selected = nullptr;
        std::vector<uint8_t> library;
        for (const char* candidate : {"assets/guest_libs/libDeadSpace.so", "lib/armeabi/libDeadSpace.so", "lib/armeabi-v7a/libDeadSpace.so"}) {
            auto found = std::find_if(archive.entries.begin(), archive.entries.end(), [&](const Entry& e) { return e.name == candidate; });
            if (found == archive.entries.end()) continue;
            auto data = extract(archive, *found);
            if (!arm32(data)) {
                LOGW("[apk-import] candidate is not an ELF32 little-endian ARM shared library: %s", candidate);
                continue;
            }
            selected = &*found;
            library = std::move(data);
            break;
        }
        if (!selected) {
            bool arm64 = std::any_of(archive.entries.begin(), archive.entries.end(),
                                     [](const Entry& e) { return e.name.starts_with("lib/arm64-v8a/"); });
            throw ImportError(arm64 ? "ARM64 library entries found, but no supported ARM32 libDeadSpace.so game core. "
                                      "Android ARM64 runtimes are not PC game libraries."
                                    : "no supported ELF32 ARM libDeadSpace.so found in this APK");
        }
        const bool atc = selected->name.starts_with("lib/");
        const std::string hash = sha256::digest(library);
        std::string profile = "unrecognized-arm32";
        if (hash == "2fc87e9a5afa1028d91a9770d23ffd04b45fb5dbc8d3800ed6b31e34223a490b") profile = "baseline-arm32";
        else if (hash == "11d718cdf87ed319cd4214dc26284cab422756561683bfb777b61b5a2c1d3b11") profile = "cpu-detection-variant-arm32";
        LOGI("[apk-import] core=%s bytes=%zu sha256=%s profile=%s", selected->name.c_str(), library.size(), hash.c_str(), profile.c_str());
        LOGI("[apk-import] texture-mode=%s (layout policy, not an APK filename or vendor-authenticity claim)", atc ? "ATC" : "legacy converted ETC2");
        if (profile == "unrecognized-arm32")
            LOGW("[apk-import] unknown game-core hash; generic ARM32 import only, runtime/hook compatibility is UNVERIFIED");
        if (profile == "cpu-detection-variant-arm32")
            LOGW("[apk-import] recognized CPU-detection variant; existing layout, independent user runtime/audio confirmation still required");

        std::vector<const Entry*> assets;
        uint64_t bytes = 0;
        bool published = false;
        size_t skipped = 0;
        for (const auto& e : archive.entries) {
            if (!e.name.starts_with("assets/") || e.name.back() == '/' || e.name.starts_with("assets/guest_libs/")) continue;
            auto lower = folded(e.name);
            if (lower == "assets/var" || lower == "assets/userdata" || lower.starts_with("assets/var/") || lower.starts_with("assets/userdata/")) {
                ++skipped;
                continue;
            }
            require(e.usize <= max_member && bytes <= max_assets - e.usize, "asset collection exceeds bounded import limits");
            bytes += e.usize;
            assets.push_back(&e);
            published |= e.name.starts_with("assets/published/");
        }
        require(published, "APK has no published game asset tree");
        if (skipped) LOGW("[apk-import] excluded %zu packaged profile/save files; no userdata imported", skipped);

        fs::path dest = fs::absolute(path(destination)).lexically_normal();
        while (!dest.has_filename() && dest != dest.root_path()) dest = dest.parent_path();
        require(dest.has_filename() && dest != dest.root_path() && plain(dest), "unsafe/reparse game-data destination: " + clean(destination));
        // Reject existing symlink/junction ancestors too, before creating parents.
        for (auto ancestor = dest.parent_path(); !ancestor.empty();) {
            require(plain(ancestor), "unsafe/reparse destination ancestor: " + clean(text(ancestor)));
            auto parent = ancestor.parent_path();
            if (parent == ancestor) break;
            ancestor = parent;
        }
        fs::create_directories(dest.parent_path());
        Stage stage;
        stage.root = new_stage(dest.parent_path());
        for (const Entry* e : assets) write_new(stage.root / path(e->name), extract(archive, *e), faults);
        write_new(stage.root / "libDeadSpace.so", library, faults);
        if (atc) write_new(stage.root / "atc_textures", "Original-layout ATC textures; selected by APK import profile.\n", faults);
        std::string info = "format=1\napk_filename=" + clean(text(path(apk_path).filename())) + "\nlibrary_member=" + selected->name +
                           "\nlibrary_sha256=" + hash + "\ncore_profile=" + profile + "\ntexture_mode=" + (atc ? "ATC" : "ETC2") +
                           "\nasset_count=" + std::to_string(assets.size()) + "\nasset_bytes=" + std::to_string(bytes) + "\n";
        write_new(stage.root / "IMPORT_INFO.txt", info, faults);
        if (fs::exists(dest)) {
            require(plain(dest) && fs::is_directory(dest), "existing destination is not a plain game-data directory");
            bool same = fs::exists(dest / "atc_textures") == atc && same_file(stage.root / "libDeadSpace.so", dest / "libDeadSpace.so") &&
                        same_tree(stage.root / "assets", dest / "assets");
            require(same, "destination already exists with different or incomplete data; nothing overwritten. Choose a fresh --game-dir "
                          "(or launch existing data without --apk): " + clean(text(dest)));
            LOGI("[apk-import] REUSED identical existing game data path=%s; files/metadata/userdata left unchanged", clean(text(dest)).c_str());
            return true;
        }
        require(!(faults && faults->publish_error), "synthetic publication failure; destination unchanged");
        require(plain(dest), "unsafe/reparse game-data destination before publication: " + clean(text(dest)));
        publish(stage.root, dest);
        stage.root.clear();
        LOGI("[apk-import] COMMITTED %zu assets (%llu bytes) path=%s; game not started by importer", assets.size(),
             (unsigned long long)bytes, clean(text(dest)).c_str());
        return true;
    } catch (const std::exception& error) {
        LOGE("[apk-import] FAILED: %s", error.what());
        return false;
    }
}
}  // namespace apk
