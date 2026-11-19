#pragma once
#include "performance.h"
struct SDL_Window;
namespace perf {
void runtime_window(SDL_Window* window);  // borrowed on main thread, queried only while alive
// Startup-only; longer intervals reduce diagnostic I/O during full playthroughs.
bool runtime_interval(unsigned seconds);
// Main/render thread only, at a completed frame. No GPU queries or guest calls.
void runtime_frame(Frame frame, uint32_t window_flags, int effective_cap, bool fast_forward, bool turbo);
void runtime_finish();
double runtime_worst_ms();  // last 180 completed frames, for the diagnostic FPS overlay
}
