// Pure contextual input dispatch, preset selection, controller-family naming,
// run-toggle policy and owned-weapon cycling helpers.
// Shared by keyboard/mouse and controller routing so every rule is defined once and unit-tested.
#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace input {

enum class ControlPreset {
    classic,
    modern,
};

enum class PadFamily {
    xbox,
    playstation,  // PS4 / PS5
    ps3,
};

// Fresh installs (no port_settings.ini) default to Modern. Existing port_settings.ini files
// without an explicit control_preset stay on Classic Port so upgrading users keep their setup.
inline ControlPreset resolve_initial_preset(bool settings_file_existed, const std::string& saved_value) {
    if (saved_value == "modern") return ControlPreset::modern;
    if (saved_value == "classic") return ControlPreset::classic;
    return settings_file_existed ? ControlPreset::classic : ControlPreset::modern;
}

inline const char* preset_setting_name(ControlPreset p) {
    return p == ControlPreset::modern ? "modern" : "classic";
}

// Classify controller family from SDL_GameControllerType values without pulling SDL headers here.
// SDL_CONTROLLER_TYPE_PS3 = 3, SDL_CONTROLLER_TYPE_PS4 = 4, SDL_CONTROLLER_TYPE_PS5 = 7.
inline PadFamily classify_pad_family(int sdl_controller_type) {
    if (sdl_controller_type == 3) return PadFamily::ps3;
    if (sdl_controller_type == 4 || sdl_controller_type == 7) return PadFamily::playstation;
    return PadFamily::xbox;
}

// Default RIG button for a controller given preset, controller family and exposed touchpad button.
// Touchpad means physical button click, never touch/swipe. If hidden by Steam Input or an adapter
// (or on PS3/Xbox), fall back to Back/Select/Share so RIG is never inaccessible.
inline int default_rig_pad_code(ControlPreset preset, PadFamily family, bool has_touchpad,
                                int back_code, int touchpad_code) {
    if (preset == ControlPreset::modern && family == PadFamily::playstation && has_touchpad)
        return touchpad_code;
    return back_code;
}

// Runtime safety fallback: if an action is bound to Touchpad (e.g. saved from a PS5) and the
// currently connected controller has no exposed touchpad button, fall back so it stays accessible.
inline int effective_pad_code(int configured_code, int fallback_code, int touchpad_code, bool has_touchpad) {
    if (configured_code == touchpad_code && !has_touchpad) return fallback_code;
    return configured_code;
}

// Family-aware display label for a controller button offset (code - kPadCode).
// Matches SDL_GameControllerButton values (0..20, where 20 = TOUCHPAD) plus 100=LT/L2, 101=RT/R2.
inline const char* pad_display_name(int button_offset, PadFamily family) {
    const bool ps = family == PadFamily::playstation || family == PadFamily::ps3;
    switch (button_offset) {
    case 0: return ps ? "Cross" : "A";
    case 1: return ps ? "Circle" : "B";
    case 2: return ps ? "Square" : "X";
    case 3: return ps ? "Triangle" : "Y";
    case 4: return family == PadFamily::ps3 ? "Select" : family == PadFamily::playstation ? "Share" : "View";
    case 6: return family == PadFamily::playstation ? "Options" : family == PadFamily::ps3 ? "Start" : "Menu";
    case 7: return "L3";
    case 8: return "R3";
    case 9: return ps ? "L1" : "LB";
    case 10: return ps ? "R1" : "RB";
    case 11: return "D-pad Up";
    case 12: return "D-pad Down";
    case 13: return "D-pad Left";
    case 14: return "D-pad Right";
    case 20: return "Touchpad";
    case 100: return ps ? "L2" : "LT";
    case 101: return ps ? "R2" : "RT";
    }
    return nullptr;
}

struct ActionState {
    bool weapon_raised = false;
    bool aim_held = false;
    bool holding_kinesis = false;

    bool aiming() const { return weapon_raised || aim_held; }
};

enum class DispatchResult {
    none,
    fire,
    throw_kinesis,
    pending_fire,
    melee,
    stomp,
    alt_fire,
};

// Keyboard LMB (Fire / Melee / Throw):
//   - carrying a kinesis object -> throw (never shoot or swing)
//   - weapon raised -> fire
//   - aim held while weapon is still raising -> pending fire
//   - weapon lowered -> melee
inline DispatchResult resolve_keyboard_fire(ActionState s) {
    if (s.holding_kinesis) return DispatchResult::throw_kinesis;
    if (s.weapon_raised) return DispatchResult::fire;
    if (s.aim_held) return DispatchResult::pending_fire;
    return DispatchResult::melee;
}

// Keyboard dedicated melee (V / Mouse 4):
//   - Modern: carrying a kinesis object throws instead of swinging; otherwise melee (including aiming)
//   - Classic: melee
inline DispatchResult resolve_keyboard_melee(ActionState s, ControlPreset preset = ControlPreset::modern) {
    if (preset == ControlPreset::modern && s.holding_kinesis) return DispatchResult::throw_kinesis;
    return DispatchResult::melee;
}

// Keyboard stomp (Space in Modern, C in Classic):
//   - Modern: stomp only with weapon lowered; no-op while aiming (not an invented throw)
//   - Classic: passes stomp directly to the game
inline DispatchResult resolve_keyboard_stomp(ActionState s, ControlPreset preset = ControlPreset::modern) {
    if (preset == ControlPreset::modern && s.aiming()) return DispatchResult::none;
    return DispatchResult::stomp;
}

// Weapon firing-mode switch (X / Middle Mouse, and controller RB/R1 while aiming):
//   - Modern: only when weapon is raised with aim held, and never while carrying a kinesis object
//   - Classic: passes alt-fire to the game (except while carrying in modern)
inline DispatchResult resolve_mode_switch(ActionState s, ControlPreset preset = ControlPreset::modern) {
    if (preset == ControlPreset::classic) return DispatchResult::alt_fire;
    if (s.holding_kinesis) return DispatchResult::none;
    return (s.weapon_raised && s.aim_held) ? DispatchResult::alt_fire : DispatchResult::none;
}

struct PadCombatFlags {
    bool fire = false;
    bool stomp = false;
    bool melee = false;
    bool alt_fire = false;
};

// Controller combat button dispatch:
//   - Modern (RT/R2 = fire + stomp, RB/R1 = melee + alt_fire):
//     Held-object priority comes first: fire or melee button throws regardless of aiming,
//     never also shooting, stomping, swinging or switching mode.
//     Outside kinesis: fire and alt_fire require aiming; stomp and melee require weapon lowered.
//   - Classic (RT = fire/melee, RB = stomp/alt_fire):
//     Preserves original RT fire/melee/throw and RB stomp/alt_fire split.
inline DispatchResult resolve_pad_combat(PadCombatFlags f, ActionState s,
                                         ControlPreset preset = ControlPreset::modern) {
    if (preset == ControlPreset::classic) {
        if (f.fire) return resolve_keyboard_fire(s);
        if (f.alt_fire && s.aiming()) return DispatchResult::alt_fire;
        if (f.stomp && !s.aiming()) return DispatchResult::stomp;
        if (f.melee) return resolve_keyboard_melee(s, preset);
        return DispatchResult::none;
    }
    if (s.holding_kinesis) {
        if (f.fire || f.melee) return DispatchResult::throw_kinesis;
        if (f.stomp && !s.aiming()) return DispatchResult::stomp;
        return DispatchResult::none;
    }
    if (f.fire && s.aiming()) return s.weapon_raised ? DispatchResult::fire : DispatchResult::pending_fire;
    if (f.alt_fire && s.weapon_raised && s.aim_held) return DispatchResult::alt_fire;
    if (f.stomp && !s.aiming()) return DispatchResult::stomp;
    if (f.melee && !s.aiming()) return DispatchResult::melee;
    return DispatchResult::none;
}

// In Classic Port, Quick Turn and Zero-G Jump share B (split by aiming).
// In Modern, they have dedicated bindings and work regardless of aiming.
inline bool pad_quick_turn_allowed(bool aiming, ControlPreset preset) {
    return preset == ControlPreset::modern || !aiming;
}

inline bool pad_jump_allowed(bool aiming, ControlPreset preset) {
    return preset == ControlPreset::modern || aiming;
}

// Run-toggle policy shared by keyboard (Caps Lock) and controller (L3):
//   - Stopping movement or raising aim clears the toggle.
//   - Hold-run OR toggle requests running, subject to forward movement and not aiming.
inline bool update_run_toggle(bool current_toggle, bool moving, bool aim_held) {
    return (moving && !aim_held) ? current_toggle : false;
}

inline bool resolve_run_requested(bool hold_run, bool run_toggle, bool moving_forward, bool aim_held) {
    return (hold_run || run_toggle) && moving_forward && !aim_held;
}

// Aim-hold synchronization between physical RMB/LT state and native weapon state:
// - While aim is held, once the player returns to normal locomotion (state 0) with the weapon
//   lowered (e.g. after a 180 quick turn or kinesis release), request a raise-only action.
// - While aim is released, keep requesting lower until the weapon is down (outside kinesis).
inline bool should_raise_aim(bool aim_held, bool weapon_raised, int player_state) {
    return aim_held && !weapon_raised && player_state == 0;
}

inline bool should_lower_aim(bool aim_held, bool weapon_raised, bool pending_fire, int player_state) {
    return !aim_held && weapon_raised && !pending_fire && player_state != 0x12;
}

// Shared owned-gun cycling for mouse wheel and controller D-pad Left / Right.
// Considers only the four gun slots (0..3); slot 4 (stasis module) is never selectable.
// Returns the next owned slot in `dir` (-1 previous, +1 next), or -1 when no other gun is owned
// (single-gun no-op so the current gun is not re-selected).
inline int cycle_owned_weapon(int current, const std::array<bool, 4>& owned, int dir) {
    if (dir == 0) return -1;
    const int step = dir > 0 ? 1 : -1;
    if (current >= 0 && current < 4) {
        for (int i = 1; i <= 3; ++i) {
            const int w = (current + step * i + 4) % 4;
            if (owned[std::size_t(w)]) return w;
        }
        return -1;
    }
    for (int i = 0; i < 4; ++i) {
        const int w = step > 0 ? i : (3 - i);
        if (owned[std::size_t(w)]) return w;
    }
    return -1;
}

}  // namespace input
