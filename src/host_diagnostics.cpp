#include "host_diagnostics.h"
#include "log.h"

#include <SDL.h>
#include <array>
#include <cstdlib>
#ifdef __linux__
#include <cerrno>
#include <sys/utsname.h>
#endif

namespace host_diagnostics {
namespace {
// Environment/error strings must not inject log lines or grow without bound.
// No complete environment, display socket paths, hostnames or WSL distro names.
struct Label {
    std::array<char, 129> bytes{};
    explicit Label(const char* value) {
        if (!value || !*value) value = "unset";
        size_t i = 0;
        for (; i < bytes.size() - 1 && value[i]; ++i) {
            const unsigned char ch = static_cast<unsigned char>(value[i]);
            bytes[i] = ch < 32 || ch == 127 || ch == '"' || ch == '\\' ? '?' : char(ch);
        }
        if (i == bytes.size() - 1 && value[i]) bytes[i - 3] = bytes[i - 2] = bytes[i - 1] = '.';
    }
    const char* text() const { return bytes.data(); }
};
#ifdef __linux__
bool present(const char* name) {
    const char* value = std::getenv(name);
    return value && *value;
}
#endif
}  // namespace

void startup() {
    SDL_version compiled{}, runtime{};
    SDL_VERSION(&compiled);
    SDL_GetVersion(&runtime);
    const Label platform(SDL_GetPlatform()), driver(SDL_GetCurrentVideoDriver());
    const Label request(std::getenv("SDL_VIDEODRIVER"));
    LOGI("host: platform=\"%s\" sdl_compiled=%u.%u.%u sdl_runtime=%u.%u.%u video_driver=\"%s\" video_driver_request=\"%s\"",
         platform.text(), unsigned(compiled.major), unsigned(compiled.minor), unsigned(compiled.patch),
         unsigned(runtime.major), unsigned(runtime.minor), unsigned(runtime.patch), driver.text(), request.text());
#ifdef __linux__
    utsname system{};
    if (uname(&system) == 0) {
        const Label kernel(system.release), machine(system.machine);
        LOGI("host_linux: kernel_valid=1 release=\"%s\" machine=\"%s\"", kernel.text(), machine.text());
    } else {
        LOGI("host_linux: kernel_valid=0 errno=%d", errno);
    }
    const Label session(std::getenv("XDG_SESSION_TYPE")), desktop(std::getenv("XDG_CURRENT_DESKTOP"));
    LOGI("host_linux: xdg_session_type=\"%s\" xdg_current_desktop=\"%s\" wsl_env_hint=%d (environment hints only; x11 may be XWayland, wayland may be nested)",
         session.text(), desktop.text(), present("WSL_INTEROP") || present("WSL_DISTRO_NAME"));
#endif
}

void display(SDL_Window* window) {
    const int index = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode mode{};
    const bool valid = index >= 0 && SDL_GetCurrentDisplayMode(index, &mode) == 0;
    const Label error(valid ? nullptr : SDL_GetError());
    int width = 0, height = 0, drawable_width = 0, drawable_height = 0;
    SDL_GetWindowSize(window, &width, &height);
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    LOGI("host_display: index=%d mode_valid=%d refresh_hz=%d refresh_known=%d sdl_swap_interval=%d window=%dx%d drawable=%dx%d (SDL-reported configuration, not measured presentation cadence)",
         index, valid, valid ? mode.refresh_rate : 0, valid && mode.refresh_rate > 0,
         SDL_GL_GetSwapInterval(), width, height, drawable_width, drawable_height);
    if (!valid) LOGI("host_display: mode query unavailable error=\"%s\"", error.text());
}

void mouse_capture(bool requested, int result, const char* message) {
    const Label error(result < 0 ? message : "none");
    const int relative = SDL_GetRelativeMouseMode();
    if (result < 0)
        LOGW("input: relative mouse request=%d result=%d sdl_relative=%d error=\"%s\" (SDL state, not proof of host confinement)",
             requested, result, relative, error.text());
    else
        LOGI("input: relative mouse request=%d result=%d sdl_relative=%d (SDL state, not proof of host confinement)",
             requested, result, relative);
}
}  // namespace host_diagnostics
