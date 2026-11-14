// Port input must respect the native HUD route and scripted player sequences.
#pragma once

#include <cstdint>

namespace input {

inline bool gameplay_input_allowed(bool current_player, bool alive, bool paused, bool cinematic, bool dialog) {
    return current_player && alive && !paused && !cinematic && !dialog;
}

// Names describe the recovered behaviour, not original enum spellings.
enum class HudInputRoute { player = 0, grapple = 1, object = 2, hidden = 3 };
inline bool hud_routes_player_input(int route, bool hidden_passthrough) {
    return route == int(HudInputRoute::player) || route == int(HudInputRoute::grapple) ||
           (route == int(HudInputRoute::hidden) && hidden_passthrough);
}

inline bool bench_store_sequence(int player_state) {
    // enterBench/enterStore -> 19; exitBenchOrStore -> 21; upgradeRig -> 22;
    // exitRigUpgrade -> 24. Animation callbacks advance through 20 and 23.
    return player_state >= 19 && player_state <= 24;
}

inline bool full_player_freeze(unsigned freeze_flags) {
    constexpr unsigned freeze_facing_and_position = 3;
    return (freeze_flags & freeze_facing_and_position) == freeze_facing_and_position;
}

inline bool scripted_input_allowed(int route, bool hidden_passthrough, int player_state, unsigned freeze_flags) {
    return hud_routes_player_input(route, hidden_passthrough) && !bench_store_sequence(player_state) &&
           !full_player_freeze(freeze_flags);
}

inline bool scripted_pointer_blocked(int route, int player_state, unsigned freeze_flags) {
    // Object-owned menus still need pointer clicks. Once they release input,
    // a locked player must not receive those same clicks as mobile touch input.
    return route != int(HudInputRoute::object) && (bench_store_sequence(player_state) || full_player_freeze(freeze_flags));
}

inline bool queued_gameplay_input_allowed(bool owned, std::uint32_t queued_epoch, std::uint32_t current_epoch) {
    return owned && queued_epoch == current_epoch;
}

inline bool free_look_input_allowed(bool owned, bool can_change_facing) {
    return owned && can_change_facing;
}

inline bool should_capture_gameplay(bool gameplay, bool free_cursor, bool focused, bool scripted) {
    return gameplay && !free_cursor && (focused || scripted);
}

}  // namespace input
