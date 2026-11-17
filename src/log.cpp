#include "log.h"
#include "host_file.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <cstdarg>
#include <mutex>

namespace logging {

bool g_verbose = false;
static std::FILE* g_file = nullptr;
static std::mutex g_lock;

void open_file(const char* path) {
    std::FILE* file = host_file::open(path, "w");
    const int error = errno;
    std::lock_guard lk(g_lock);
    if (g_file) std::fclose(g_file);
    g_file = file;
    if (!file) std::fprintf(stderr, "Cannot open log %s: %s\n", path, std::strerror(error));
}
void close_file() {
    std::lock_guard lk(g_lock);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
}

void write(char level, const char* fmt, ...) {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    double t = duration<double>(steady_clock::now() - t0).count();
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::lock_guard lk(g_lock);
    std::fprintf(stdout, "%9.3f %c %s\n", t, level, buf);
    if (level == 'E') std::fflush(stdout);
    if (g_file) {
        std::fprintf(g_file, "%9.3f %c %s\n", t, level, buf);
        std::fflush(g_file);
    }
}

}  // namespace logging
