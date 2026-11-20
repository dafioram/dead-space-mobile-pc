#pragma once
#include <cstdint>

// Declaration-only adapter for production logging tests. Deliberately no SDL
// init, window creation, input setters or game execution are provided.
struct SDL_Window {};
struct SDL_version { uint8_t major, minor, patch; };
inline void SDL_VERSION(SDL_version* version) { *version = {2, 0, 0}; }
struct SDL_DisplayMode { uint32_t format; int w, h, refresh_rate; void* driverdata; };
const char* SDL_GetPlatform();
const char* SDL_GetCurrentVideoDriver();
void SDL_GetVersion(SDL_version* version);
int SDL_GetWindowDisplayIndex(SDL_Window* window);
int SDL_GetCurrentDisplayMode(int display, SDL_DisplayMode* mode);
const char* SDL_GetError();
void SDL_GetWindowSize(SDL_Window* window, int* width, int* height);
void SDL_GL_GetDrawableSize(SDL_Window* window, int* width, int* height);
int SDL_GL_GetSwapInterval();
int SDL_GetRelativeMouseMode();
