// bionic stdio (FILE*) on top of host stdio, plus the guest->host path mapping.
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <vector>
#include <cerrno>
#include <limits>
#ifdef _WIN32
#include <io.h>
#endif

#include "hle/hle.h"
#include "log_path.h"
#include "save_io.h"
#include "hle/io_buffer.h"

namespace hle {

std::string g_assets_dir;
std::string g_data_dir;

// Every absolute guest path lives inside the sandbox root g_data_dir.
std::string host_path(const std::string& guest) {
    std::string p = guest;
    std::replace(p.begin(), p.end(), '\\', '/');  // also contain Windows-style parent traversal
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    // collapse "./" and duplicate slashes; refuse to climb out of the sandbox
    std::string out;
    size_t i = 0;
    std::vector<std::string> parts;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string part = p.substr(i, j - i);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        i = j + 1;
    }
    out = g_data_dir;
    for (auto& s : parts) out += "/" + s;
    return out;
}

namespace {

constexpr u32 FILE_SIZE = 84;
u32 g_sf = 0;  // __sF

std::mutex g_files_lock;
std::map<u32, std::FILE*> g_files;  // guest FILE* -> host FILE*

std::FILE* host_file(u32 gf) {
    if (gf == g_sf) return stdin;
    if (gf == g_sf + FILE_SIZE) return stdout;
    if (gf == g_sf + 2 * FILE_SIZE) return stderr;
    std::lock_guard lk(g_files_lock);
    auto it = g_files.find(gf);
    return it == g_files.end() ? nullptr : it->second;
}

void io_error(std::FILE* stream, const char* operation, int error) {
    if (!error) error = EIO;
    if (stream) save_io::fail_stream(stream, operation, error);
    set_errno(host_errno_to_guest(error));
}

}  // namespace

// used by printf.cpp for fprintf/vfprintf
std::FILE* stdio_host_file(u32 gf) { return host_file(gf); }

void register_stdio() {
    g_sf = resolve("__sF", false, false);

    reg("fopen", [](Ctx& c) {
        std::string gp = c.argstr(0);
        std::string mode = c.argstr(1);
        std::string hp = host_path(gp);
        if (mode.empty()) { set_errno(G_EINVAL); return c.ret(0); }
        if (mode[0] == 'w' || mode[0] == 'a') {
            std::error_code ec;
            std::filesystem::create_directories(save_io::utf8_path(hp).parent_path(), ec);
            if (ec) {
                LOGE("[save-io] fopen parent creation failed path=%s reason=%s", logging::display_path(hp).c_str(), ec.message().c_str());
                set_errno(ec == std::errc::permission_denied ? G_EACCES : G_EIO);
                return c.ret(0);
            }
        }
        std::FILE* f = save_io::fopen(save_io::utf8_path(hp), mode);
        int error = errno;
        LOGD("fopen(%s, %s) -> %s", gp.c_str(), c.argstr(1), f ? "ok" : "FAIL");
        if (!f) {
            set_errno(host_errno_to_guest(error));
            return c.ret(0);
        }
        u32 gf = mem::heap_alloc(FILE_SIZE);
        std::memset(mem::host(gf), 0, FILE_SIZE);
#ifdef _WIN32
        mem::w16(gf + 14, u16(_fileno(f)));
#else
        mem::w16(gf + 14, u16(fileno(f)));
#endif
        std::lock_guard lk(g_files_lock);
        g_files[gf] = f;
        c.ret(gf);
    });
    reg("fclose", [](Ctx& c) {
        u32 gf = c.arg(0);
        std::FILE* f = nullptr;
        {
            std::lock_guard lk(g_files_lock);
            auto it = g_files.find(gf);
            if (it != g_files.end()) {
                f = it->second;
                g_files.erase(it);
            }
        }
        if (!f) { set_errno(G_EBADF); return c.ret(0xFFFFFFFF); }
        int result = save_io::fclose(f);
        if (result != 0) set_errno(host_errno_to_guest(errno));
        mem::heap_free(gf);
        c.ret(u32(result));
    });
    reg("fread", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(3));
        if (!f) { set_errno(G_EBADF); return c.ret(0); }
        uint64_t bytes = uint64_t(c.arg(1)) * c.arg(2);
        if (bytes > UINT32_MAX || !io_buffer(c.arg(0), bytes)) {
            io_error(f, "invalid guest fread range", bytes > UINT32_MAX ? EOVERFLOW : EFAULT);
            return c.ret(0);
        }
        size_t result = save_io::fread(mem::host(c.arg(0)), c.arg(1), c.arg(2), f);
        if (std::ferror(f)) set_errno(host_errno_to_guest(errno ? errno : EIO));
        c.ret(u32(result));
    });
    reg("fwrite", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(3));
        if (!f) { set_errno(G_EBADF); return c.ret(0); }
        uint64_t bytes = uint64_t(c.arg(1)) * c.arg(2);
        if (bytes > UINT32_MAX || !io_buffer(c.arg(0), bytes)) {
            io_error(f, "invalid guest fwrite range", bytes > UINT32_MAX ? EOVERFLOW : EFAULT);
            return c.ret(0);
        }
        size_t result = save_io::fwrite(mem::host(c.arg(0)), c.arg(1), c.arg(2), f);
        if (result != c.arg(2) && c.arg(1)) set_errno(host_errno_to_guest(errno ? errno : EIO));
        c.ret(u32(result));
    });
    reg("fseek", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(0));
        if (!f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
        int result = std::fseek(f, long(s32(c.arg(1))), int(c.arg(2)));
        if (result != 0) io_error(f, "fseek", errno);
        c.ret(u32(result));
    });
    reg("ftell", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(0));
        if (!f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
        long result = std::ftell(f);
        if (result < 0) io_error(f, "ftell", errno);
        else if (uint64_t(result) > INT32_MAX) { io_error(f, "ftell overflow", EOVERFLOW); return c.ret(0xffffffff); }
        c.ret(u32(result));
    });
    reg("fflush", [](Ctx& c) {
        std::FILE* f = c.arg(0) ? host_file(c.arg(0)) : nullptr;
        if (c.arg(0) && !f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
        int result = save_io::fflush(f);
        if (result != 0) set_errno(host_errno_to_guest(errno ? errno : EIO));
        c.ret(u32(result));
    });
    reg("fgetc", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(0));
        if (!f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
        int result = std::fgetc(f);
        if (std::ferror(f)) io_error(f, "fgetc", errno);
        c.ret(u32(result));
    });
    reg("ungetc", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(1));
        if (!f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
        int result = std::ungetc(int(c.arg(0)), f);
        if (result == EOF && std::ferror(f)) io_error(f, "ungetc", errno);
        c.ret(u32(result));
    });
    reg("fgets", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(2));
        if (!f) { set_errno(G_EBADF); return c.ret(0); }
        if (s32(c.arg(1)) <= 0 || !io_buffer(c.arg(0), c.arg(1))) {
            io_error(f, "invalid guest fgets range", EINVAL); return c.ret(0);
        }
        char* r = std::fgets(c.argp<char>(0), int(c.arg(1)), f);
        if (std::ferror(f)) io_error(f, "fgets", errno);
        c.ret(r ? c.arg(0) : 0);
    });
    reg("fputs", [](Ctx& c) {
        std::FILE* f = host_file(c.arg(1));
        if (!f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
        std::string text = c.argstr(0);
        size_t n = save_io::fwrite(text.data(), 1, text.size(), f);
        c.ret(n == text.size() ? 0 : 0xffffffff);
        if (n != text.size()) set_errno(host_errno_to_guest(errno ? errno : EIO));
    });
    reg("ferror", [](Ctx& c) { auto* f = host_file(c.arg(0)); if (!f) set_errno(G_EBADF); c.ret(f ? u32(save_io::stream_failed(f)) : 1); });
    reg("feof", [](Ctx& c) { auto* f = host_file(c.arg(0)); if (!f) set_errno(G_EBADF); c.ret(f ? u32(std::feof(f)) : 0); });
    reg("clearerr", [](Ctx& c) { auto* f = host_file(c.arg(0)); if (f) save_io::clear_stream_error(f); else set_errno(G_EBADF); });
    reg("fileno", [](Ctx& c) {
        auto* f = host_file(c.arg(0));
        if (!f) { set_errno(G_EBADF); return c.ret(0xffffffff); }
#ifdef _WIN32
        c.ret(u32(_fileno(f)));
#else
        c.ret(u32(fileno(f)));
#endif
    });
    reg("fwide", [](Ctx& c) { c.ret(c.arg(1)); });
}

}  // namespace hle
