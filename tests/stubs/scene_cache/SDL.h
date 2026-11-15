// Declaration-only stand-ins for the asset-free lights.cpp adapter test.
// This directory is on that test target's include path only, never the app's.
#pragma once
#include <cstdint>
struct SDL_Window;
union SDL_Event;
uint32_t SDL_GetTicks();
