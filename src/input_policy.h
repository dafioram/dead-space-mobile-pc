// Input ownership: a dead player must never keep UI clicks in gameplay capture.
#pragma once

namespace input {

inline bool gameplay_input_allowed(bool current_player, bool alive, bool paused, bool cinematic, bool dialog) {
    return current_player && alive && !paused && !cinematic && !dialog;
}

inline bool should_capture_gameplay(bool gameplay, bool free_cursor, bool focused, bool scripted) {
    return gameplay && !free_cursor && (focused || scripted);
}

}  // namespace input
