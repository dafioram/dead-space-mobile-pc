// Pure contextual input dispatch and owned-weapon cycling helpers.
// Shared by keyboard/mouse and controller routing so held-object priority,
// aiming-dependent splits and single-gun no-op rules are defined once and unit-tested.
#pragma once

#include <array>
#include <cstddef>

namespace input {

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
//   - carrying a kinesis object -> throw (takes priority over melee)
//   - otherwise -> melee, including while aiming
inline DispatchResult resolve_keyboard_melee(ActionState s) {
    return s.holding_kinesis ? DispatchResult::throw_kinesis : DispatchResult::melee;
}

// Keyboard stomp (Space):
//   - weapon lowered -> stomp (subject to native eligibility; not an invented throw)
//   - aiming -> no-op
inline DispatchResult resolve_keyboard_stomp(ActionState s) {
    return s.aiming() ? DispatchResult::none : DispatchResult::stomp;
}

// Weapon firing-mode switch (X / Middle Mouse, and controller RB/R1 while aiming):
//   - only when weapon is raised with aim held, and never while carrying a kinesis object
inline DispatchResult resolve_mode_switch(ActionState s) {
    if (s.holding_kinesis) return DispatchResult::none;
    return (s.weapon_raised && s.aim_held) ? DispatchResult::alt_fire : DispatchResult::none;
}

struct PadCombatFlags {
    bool fire = false;
    bool stomp = false;
    bool melee = false;
    bool alt_fire = false;
};

// Controller combat button dispatch (e.g. RT/R2 = fire + stomp, RB/R1 = melee + alt_fire):
//   - Held-object priority comes first: any button carrying fire or melee throws while carrying,
//     regardless of aiming, and never also fires, stomps, swings or switches mode.
//   - Outside kinesis: fire and alt_fire require aiming; stomp and melee require weapon lowered.
inline DispatchResult resolve_pad_combat(PadCombatFlags f, ActionState s) {
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
