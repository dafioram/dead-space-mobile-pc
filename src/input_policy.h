// Port input must respect the native HUD route and scripted player sequences.
#pragma once

#include <cstdint>

namespace input {

inline bool gameplay_input_allowed(bool current_player, bool alive, bool paused, bool cinematic, bool dialog) {
    return current_player && alive && !paused && !cinematic && !dialog;
}

// Names describe the recovered behaviour, not original enum spellings.
enum class HudInputRoute { player = 0, grapple = 1, object = 2, hidden = 3 };

inline bool recent_cinematic(bool present, std::uint32_t now, std::uint32_t updated) {
    return present && now - updated < 200;
}

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

inline bool interactive_scripted_input(bool cinematic, int player_state, int hud_route, bool grabbed) {
    // isGrappling(false) recognises states 9/10, including handoff frames before
    // a gesture phase is recorded. doDementiaShakeOff selects interactive state 12.
    // An object-owned UI must keep its pointer despite a frozen player body.
    // Cinematics also use objectGetInput: route 2 alone is not an exemption for
    // a live movie or an explicit tram/generic/collection animation. Bench/store
    // UI remains interactive independently of a recent camera observation.
    const bool object_input = hud_route == int(HudInputRoute::object) &&
                              (bench_store_sequence(player_state) ||
                               (!cinematic && player_state != 27 && player_state != 29 && player_state != 30));
    return grabbed || hud_route == int(HudInputRoute::grapple) ||
           player_state == 9 || player_state == 10 || player_state == 12 || object_input;
}

inline bool scripted_player_sequence(int player_state, unsigned freeze_flags) {
    // Recovered native entry points: doIdleFrozen -> 1 (boss camera handoff),
    // doClimbOntoTram -> 27, doGenericAnim -> 29, doWeaponCollect -> 30.
    // Do not infer a movie from every temporary combat animation or camera mode.
    return player_state == 1 || bench_store_sequence(player_state) || player_state == 27 ||
           player_state == 29 || player_state == 30 || full_player_freeze(freeze_flags);
}

inline bool noninteractive_scripted_input(bool cinematic, int player_state, int hud_route,
                                         bool grabbed, unsigned freeze_flags) {
    return !interactive_scripted_input(cinematic, player_state, hud_route, grabbed) &&
           (cinematic || scripted_player_sequence(player_state, freeze_flags));
}

inline bool cinematic_skip_allowed(bool tracked_cinematic, bool noninteractive) {
    // Recognising a scripted lock must not make boss/tram/collection animations skippable.
    return tracked_cinematic && noninteractive;
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
