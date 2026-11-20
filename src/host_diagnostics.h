#pragma once

struct SDL_Window;

namespace host_diagnostics {
// Read-only context in the ordinary log; no diagnostic launch flag required.
void startup();  // after successful SDL video initialization, before window creation
void display(SDL_Window* window);  // after GL context/swap-interval setup
// Caller supplies the existing SDL_SetRelativeMouseMode result and immediately
// captured error. Observes only; never changes gameplay ownership/capture policy.
void mouse_capture(bool requested, int result, const char* error);
}  // namespace host_diagnostics
