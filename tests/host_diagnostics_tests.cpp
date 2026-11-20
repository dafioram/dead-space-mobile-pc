#include "host_diagnostics.h"
#include <SDL.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Line { char level; std::string text; };
std::vector<Line> lines;
std::string driver = "x11", error = "synthetic failure";
int index = 1, refresh = 60, swap = 1, relative = 1;
bool mode_valid = true;
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
bool contains(const char* text) {
    for (const auto& line : lines) if (line.text.find(text) != std::string::npos) return true;
    return false;
}
struct Environment {
    std::string name, before;
    bool existed;
    Environment(const char* key, const char* value) : name(key) {
        const char* old = std::getenv(key);
        existed = old != nullptr; if (old) before = old;
        set(value);
    }
    void set(const char* value) const {
#ifdef _WIN32
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1); else unsetenv(name.c_str());
#endif
    }
    ~Environment() { set(existed ? before.c_str() : nullptr); }
};
}

namespace logging {
void write(char level, const char* format, ...) {
    char text[4096]; va_list arguments; va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments); va_end(arguments);
    lines.push_back({level, text});
}
}
const char* SDL_GetPlatform() { return "synthetic host"; }
const char* SDL_GetCurrentVideoDriver() { return driver.empty() ? nullptr : driver.c_str(); }
void SDL_GetVersion(SDL_version* version) { *version = {2, 32, 10}; }
int SDL_GetWindowDisplayIndex(SDL_Window*) { return index; }
int SDL_GetCurrentDisplayMode(int, SDL_DisplayMode* mode) { mode->refresh_rate = refresh; return mode_valid ? 0 : -1; }
const char* SDL_GetError() { return error.c_str(); }
void SDL_GetWindowSize(SDL_Window*, int* width, int* height) { *width = 960; *height = 540; }
void SDL_GL_GetDrawableSize(SDL_Window*, int* width, int* height) { *width = 1920; *height = 1080; }
int SDL_GL_GetSwapInterval() { return swap; }
int SDL_GetRelativeMouseMode() { return relative; }

int main() {
    try {
        Environment request("SDL_VIDEODRIVER", "wayland"), session("XDG_SESSION_TYPE", "wayland"), desktop("XDG_CURRENT_DESKTOP", "synthetic-desktop");
        Environment interop("WSL_INTEROP", nullptr), distro("WSL_DISTRO_NAME", nullptr);
        host_diagnostics::startup();
        require(contains("sdl_compiled=2.0.0 sdl_runtime=2.32.10"), "compiled/runtime SDL versions");
        require(contains("video_driver=\"x11\" video_driver_request=\"wayland\""), "actual backend must not be inferred from environment");
#ifdef __linux__
        require(contains("kernel_valid=1") && contains("machine="), "Linux kernel/architecture queried without shell");
        require(contains("wsl_env_hint=0") && contains("x11 may be XWayland"), "unset WSL hint and session caveat");
        distro.set("DO_NOT_LOG_PRIVATE_DISTRO_NAME");
        interop.set("DO_NOT_LOG_PRIVATE_SOCKET_PATH");
        lines.clear();host_diagnostics::startup();
        require(contains("wsl_env_hint=1") && !contains("DO_NOT_LOG"), "WSL hint must not expose distro/socket values");
#endif
        lines.clear();driver = "wayland";session.set("x11");
        host_diagnostics::startup();
        require(contains("video_driver=\"wayland\""), "Wayland selected independently of session hint");
        lines.clear();driver.clear();request.set(nullptr);host_diagnostics::startup();
        require(contains("video_driver=\"unset\" video_driver_request=\"unset\""), "missing values explicit");
        const std::string oversized(1000, 'x');request.set((std::string("unsafe\n\r\t\"\\%") + oversized).c_str());
        desktop.set("unsafe\ninjected-desktop");
        lines.clear();host_diagnostics::startup();
        require(contains("...") && !contains(oversized.c_str()), "environment fields bounded and truncation visible");
        for (const auto& line : lines) require(line.text.find('\n') == std::string::npos && line.text.find('\r') == std::string::npos && line.text.size() < 1024, "no injected log lines or unbounded data");
        SDL_Window window;
        lines.clear();host_diagnostics::display(&window);
        require(contains("mode_valid=1 refresh_hz=60 refresh_known=1 sdl_swap_interval=1 window=960x540 drawable=1920x1080"), "display and drawable context");
        refresh = 0;swap = 0;lines.clear();host_diagnostics::display(&window);
        require(contains("mode_valid=1 refresh_hz=0 refresh_known=0 sdl_swap_interval=0"), "unknown refresh distinguished from failure");
        mode_valid = false;refresh = 999;error = "bad\nmode";lines.clear();host_diagnostics::display(&window);
        require(contains("mode_valid=0 refresh_hz=0 refresh_known=0") && contains("error=\"bad?mode\""), "failed query must not claim stale mode");
        index = -1;lines.clear();host_diagnostics::display(&window);
        require(contains("index=-1 mode_valid=0"), "missing display explicit");
        lines.clear();host_diagnostics::mouse_capture(true, 0, "stale error must not be logged");
        require(contains("request=1 result=0 sdl_relative=1") && lines[0].level == 'I' && !contains("stale error"), "capture success reports SDL state only");
        relative = 0;lines.clear();host_diagnostics::mouse_capture(true, -1, "unsupported\n%capture");
        require(contains("request=1 result=-1 sdl_relative=0 error=\"unsupported?%capture\"") && lines[0].level == 'W', "capture failure/error not hidden");
        require(relative == 0, "logging must not change relative mode");
        lines.clear();host_diagnostics::mouse_capture(false, 0, nullptr);
        require(contains("request=0 result=0 sdl_relative=0") && contains("not proof of host confinement"), "release and confinement caveat");
        std::cout << "host diagnostics adapter tests passed; no SDL/window/input/game initialization\n";
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
