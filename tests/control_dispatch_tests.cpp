#include "control_dispatch.h"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

using input::ActionState;
using input::DispatchResult;
using input::PadCombatFlags;

void require(bool okay, const char* why) {
    if (!okay) throw std::runtime_error(why);
}

void test_keyboard_dispatch() {
    // LMB: fire while raised, pending_fire while raising, melee when lowered, throw when carrying.
    require(input::resolve_keyboard_fire({false, false, false}) == DispatchResult::melee,
            "LMB with weapon lowered performs melee");
    require(input::resolve_keyboard_fire({false, true, false}) == DispatchResult::pending_fire,
            "LMB while aim is raising queues pending fire");
    require(input::resolve_keyboard_fire({true, true, false}) == DispatchResult::fire,
            "LMB while raised fires");
    require(input::resolve_keyboard_fire({true, false, false}) == DispatchResult::fire,
            "LMB while weapon still raised fires");

    // V / Mouse 4: dedicated melee including while aiming; throws while carrying.
    require(input::resolve_keyboard_melee({false, false, false}) == DispatchResult::melee,
            "V/Mouse 4 lowered performs melee");
    require(input::resolve_keyboard_melee({false, true, false}) == DispatchResult::melee,
            "V/Mouse 4 while raising performs melee");
    require(input::resolve_keyboard_melee({true, true, false}) == DispatchResult::melee,
            "V/Mouse 4 while aiming performs melee");

    // Space: stomp only with weapon lowered; no-op while aiming; not an invented throw while carrying.
    require(input::resolve_keyboard_stomp({false, false, false}) == DispatchResult::stomp,
            "Space with weapon lowered stomps");
    require(input::resolve_keyboard_stomp({false, true, false}) == DispatchResult::none,
            "Space while aim held does nothing");
    require(input::resolve_keyboard_stomp({true, false, false}) == DispatchResult::none,
            "Space while weapon raised does nothing");
    require(input::resolve_keyboard_stomp({true, true, false}) == DispatchResult::none,
            "Space while aiming does nothing");
    require(input::resolve_keyboard_stomp({false, false, true}) == DispatchResult::stomp,
            "Space while carrying remains subject to native stomp eligibility, not an invented throw");

    // X / Middle Mouse: switch firing mode only when raised with aim held; never when lowered or carrying.
    require(input::resolve_mode_switch({true, true, false}) == DispatchResult::alt_fire,
            "mode switch works when weapon is raised with aim held");
    require(input::resolve_mode_switch({false, false, false}) == DispatchResult::none,
            "mode switch does nothing with weapon lowered");
    require(input::resolve_mode_switch({false, true, false}) == DispatchResult::none,
            "mode switch waits until weapon is raised");
    require(input::resolve_mode_switch({true, false, false}) == DispatchResult::none,
            "mode switch requires aim held");

    // Held-object priority across every aiming/raised combination.
    for (bool raised : {false, true}) {
        for (bool aim_held : {false, true}) {
            const ActionState carrying{raised, aim_held, true};
            require(input::resolve_keyboard_fire(carrying) == DispatchResult::throw_kinesis,
                    "LMB throws while carrying regardless of aiming");
            require(input::resolve_keyboard_melee(carrying) == DispatchResult::throw_kinesis,
                    "V/Mouse 4 throws while carrying regardless of aiming");
            require(input::resolve_mode_switch(carrying) == DispatchResult::none,
                    "X/Middle Mouse does nothing while carrying");
        }
    }
}

void test_controller_dispatch() {
    // RT / R2 carries both fire (aiming) and stomp (not aiming), and throws while carrying.
    const PadCombatFlags rt{true, true, false, false};
    require(input::resolve_pad_combat(rt, {false, false, false}) == DispatchResult::stomp,
            "RT with weapon lowered stomps");
    require(input::resolve_pad_combat(rt, {false, true, false}) == DispatchResult::pending_fire,
            "RT while aim is raising queues pending fire");
    require(input::resolve_pad_combat(rt, {true, true, false}) == DispatchResult::fire,
            "RT while aiming fires");

    // RB / R1 carries melee (not aiming) and alt_fire (aiming), and throws while carrying.
    const PadCombatFlags rb{false, false, true, true};
    require(input::resolve_pad_combat(rb, {false, false, false}) == DispatchResult::melee,
            "RB with weapon lowered swings melee");
    require(input::resolve_pad_combat(rb, {false, true, false}) == DispatchResult::none,
            "RB while weapon is still raising does not swing melee or switch mode prematurely");
    require(input::resolve_pad_combat(rb, {true, true, false}) == DispatchResult::alt_fire,
            "RB while raised with aim held switches firing mode");

    // Held-object priority on RT and RB across every aiming/raised combination.
    for (bool raised : {false, true}) {
        for (bool aim_held : {false, true}) {
            const ActionState carrying{raised, aim_held, true};
            require(input::resolve_pad_combat(rt, carrying) == DispatchResult::throw_kinesis,
                    "RT throws while carrying regardless of aim");
            require(input::resolve_pad_combat(rb, carrying) == DispatchResult::throw_kinesis,
                    "RB throws while carrying regardless of aim");
        }
    }

    // Standalone rebound buttons keep their own semantics without inventing throws.
    const PadCombatFlags stomp_only{false, true, false, false};
    require(input::resolve_pad_combat(stomp_only, {false, false, true}) == DispatchResult::stomp,
            "separate stomp button while carrying does not invent a throw");
    const PadCombatFlags alt_only{false, false, false, true};
    require(input::resolve_pad_combat(alt_only, {true, true, true}) == DispatchResult::none,
            "separate mode-switch button does nothing while carrying");
}

void test_weapon_cycling() {
    // Zero owned guns -> no-op.
    const std::array<bool, 4> none{false, false, false, false};
    require(input::cycle_owned_weapon(0, none, 1) == -1 && input::cycle_owned_weapon(0, none, -1) == -1,
            "cycling with no owned guns is a no-op");

    // Single owned gun in any slot -> no-op in both directions (never re-selects current).
    for (int slot = 0; slot < 4; ++slot) {
        std::array<bool, 4> one{false, false, false, false};
        one[std::size_t(slot)] = true;
        require(input::cycle_owned_weapon(slot, one, 1) == -1, "single owned gun next is a no-op");
        require(input::cycle_owned_weapon(slot, one, -1) == -1, "single owned gun prev is a no-op");
    }

    // Two sparse guns (slots 0 and 2): skips unowned 1 and 3 and wraps both ways.
    const std::array<bool, 4> two{true, false, true, false};
    require(input::cycle_owned_weapon(0, two, 1) == 2 && input::cycle_owned_weapon(2, two, 1) == 0,
            "two owned guns wrap forward");
    require(input::cycle_owned_weapon(0, two, -1) == 2 && input::cycle_owned_weapon(2, two, -1) == 0,
            "two owned guns wrap backward");

    // Three guns (slots 0, 1, 3): skips unowned slot 2 in both directions.
    const std::array<bool, 4> three{true, true, false, true};
    require(input::cycle_owned_weapon(0, three, 1) == 1 &&
                input::cycle_owned_weapon(1, three, 1) == 3 &&
                input::cycle_owned_weapon(3, three, 1) == 0,
            "three owned guns cycle forward skipping unowned slot 2");
    require(input::cycle_owned_weapon(0, three, -1) == 3 &&
                input::cycle_owned_weapon(3, three, -1) == 1 &&
                input::cycle_owned_weapon(1, three, -1) == 0,
            "three owned guns cycle backward skipping unowned slot 2");

    // All four guns.
    const std::array<bool, 4> all{true, true, true, true};
    require(input::cycle_owned_weapon(3, all, 1) == 0 && input::cycle_owned_weapon(0, all, -1) == 3,
            "four owned guns wrap across slot boundaries");
    require(input::cycle_owned_weapon(0, all, 0) == -1, "zero direction is a no-op");

    // Out-of-range current slot (e.g. stasis slot 4 or -1) picks a valid owned gun in 0..3.
    require(input::cycle_owned_weapon(4, two, 1) == 0 && input::cycle_owned_weapon(4, two, -1) == 2,
            "invalid/stasis current slot recovers to an owned gun slot in 0..3");
}

void test_aim_hold_sync() {
    // Quick turn (state 2) lowers the weapon; while turning, do not re-raise yet.
    require(!input::should_raise_aim(true, false, 2), "do not interrupt 180 quick turn while still turning");
    // Once quick turn finishes (state 0), holding RMB/LT immediately re-raises the weapon.
    require(input::should_raise_aim(true, false, 0), "holding aim after quick turn re-raises weapon");
    // Once weapon is already raised, stop requesting raise.
    require(!input::should_raise_aim(true, true, 0), "already-raised weapon must not request raise again");
    // If RMB/LT was released during the turn, stay lowered.
    require(!input::should_raise_aim(false, false, 0), "released aim must not re-raise weapon");
    // Never request raise during kinesis (0x12), reload (6), stasis (5), grapple (9), etc.
    for (int busy_state : {2, 3, 4, 5, 6, 9, 10, 11, 0x12}) {
        require(!input::should_raise_aim(true, false, busy_state),
                "busy/non-locomotion states must wait until returning to state 0 before re-raising");
    }

    // Lowering symmetry.
    require(input::should_lower_aim(false, true, false, 0), "released aim lowers a raised weapon");
    require(!input::should_lower_aim(true, true, false, 0), "held aim does not lower");
    require(!input::should_lower_aim(false, true, true, 0), "pending fire delays lowering until shot fires");
    require(!input::should_lower_aim(false, true, false, 0x12), "do not send lower during kinesis state");
}

void test_presets_and_classic_compatibility() {
    using input::ControlPreset;
    // Fresh install (no INI on disk) -> Modern.
    require(input::resolve_initial_preset(false, "") == ControlPreset::modern,
            "fresh install without port_settings.ini defaults to Modern");
    // Existing INI without control_preset -> Classic Port (non-destructive upgrade).
    require(input::resolve_initial_preset(true, "") == ControlPreset::classic,
            "existing port_settings.ini without control_preset preserves Classic Port");
    // Explicit saved values always win regardless of file flag.
    require(input::resolve_initial_preset(true, "modern") == ControlPreset::modern,
            "explicit modern setting selects Modern");
    require(input::resolve_initial_preset(false, "classic") == ControlPreset::classic,
            "explicit classic setting selects Classic Port");
    require(input::resolve_initial_preset(true, "unknown") == ControlPreset::classic,
            "unrecognized setting falls back safely for existing INI");
    require(std::string(input::preset_setting_name(ControlPreset::modern)) == "modern" &&
                std::string(input::preset_setting_name(ControlPreset::classic)) == "classic",
            "preset setting names are stable");

    // Classic Port combat semantics remain intact.
    const PadCombatFlags classic_rt{true, false, false, false};
    const PadCombatFlags classic_rb{false, true, false, true};
    require(input::resolve_pad_combat(classic_rt, {false, false, false}, ControlPreset::classic) == DispatchResult::melee,
            "Classic RT with weapon lowered swings melee");
    require(input::resolve_pad_combat(classic_rt, {true, true, false}, ControlPreset::classic) == DispatchResult::fire,
            "Classic RT while aiming fires");
    require(input::resolve_pad_combat(classic_rt, {false, false, true}, ControlPreset::classic) == DispatchResult::throw_kinesis,
            "Classic RT while carrying throws");
    require(input::resolve_pad_combat(classic_rb, {false, false, false}, ControlPreset::classic) == DispatchResult::stomp,
            "Classic RB with weapon lowered stomps");
    require(input::resolve_pad_combat(classic_rb, {true, true, false}, ControlPreset::classic) == DispatchResult::alt_fire,
            "Classic RB while aiming switches fire mode");

    require(input::resolve_keyboard_stomp({true, true, false}, ControlPreset::classic) == DispatchResult::stomp,
            "Classic keyboard stomp passes straight through");
    require(input::resolve_keyboard_melee({false, false, true}, ControlPreset::classic) == DispatchResult::melee,
            "Classic keyboard melee stays melee");
    require(input::resolve_mode_switch({false, false, false}, ControlPreset::classic) == DispatchResult::alt_fire,
            "Classic alt-fire passes straight through");

    // Classic vs Modern controller quick-turn / zero-G jump aim filtering.
    require(!input::pad_quick_turn_allowed(true, ControlPreset::classic) &&
                input::pad_quick_turn_allowed(false, ControlPreset::classic),
            "Classic controller quick turn only fires when not aiming");
    require(input::pad_jump_allowed(true, ControlPreset::classic) &&
                !input::pad_jump_allowed(false, ControlPreset::classic),
            "Classic controller zero-G jump requires aiming");
    require(input::pad_quick_turn_allowed(true, ControlPreset::modern) &&
                input::pad_quick_turn_allowed(false, ControlPreset::modern),
            "Modern controller quick turn works aiming or not");
    require(input::pad_jump_allowed(true, ControlPreset::modern) &&
                input::pad_jump_allowed(false, ControlPreset::modern),
            "Modern controller zero-G jump works aiming or not");
}

void test_playstation_support_and_capabilities() {
    using input::ControlPreset;
    using input::PadFamily;
    constexpr int kBack = 2004;
    constexpr int kTouchpad = 2020;

    // Family classification from SDL_GameControllerType constants.
    require(input::classify_pad_family(3) == PadFamily::ps3, "SDL PS3 type classifies as ps3");
    require(input::classify_pad_family(4) == PadFamily::playstation, "SDL PS4 type classifies as playstation");
    require(input::classify_pad_family(7) == PadFamily::playstation, "SDL PS5 type classifies as playstation");
    require(input::classify_pad_family(1) == PadFamily::xbox, "Xbox 360 classifies as xbox");
    require(input::classify_pad_family(2) == PadFamily::xbox, "Xbox One classifies as xbox");
    require(input::classify_pad_family(0) == PadFamily::xbox, "Unknown/generic controller classifies as xbox");

    // Default RIG mapping respects preset, family and exposed touchpad capability.
    require(input::default_rig_pad_code(ControlPreset::modern, PadFamily::playstation, true, kBack, kTouchpad) == kTouchpad,
            "Modern PS4/PS5 with exposed touchpad defaults RIG to physical Touchpad click");
    require(input::default_rig_pad_code(ControlPreset::modern, PadFamily::playstation, false, kBack, kTouchpad) == kBack,
            "Modern PS4/PS5 without exposed touchpad falls back to Back/Share so RIG stays accessible");
    require(input::default_rig_pad_code(ControlPreset::modern, PadFamily::ps3, false, kBack, kTouchpad) == kBack,
            "Modern PS3 defaults RIG to Select");
    require(input::default_rig_pad_code(ControlPreset::modern, PadFamily::xbox, false, kBack, kTouchpad) == kBack,
            "Modern Xbox defaults RIG to View/Back");
    require(input::default_rig_pad_code(ControlPreset::classic, PadFamily::playstation, true, kBack, kTouchpad) == kBack,
            "Classic Port keeps Back/Share default even on PS4/PS5");

    // Runtime effective code fallback for saved Touchpad binding on a controller without touchpad.
    require(input::effective_pad_code(kTouchpad, kBack, kTouchpad, true) == kTouchpad,
            "saved Touchpad binding remains Touchpad when controller exposes it");
    require(input::effective_pad_code(kTouchpad, kBack, kTouchpad, false) == kBack,
            "saved Touchpad binding falls back to Back when controller lacks touchpad");
    require(input::effective_pad_code(2002, kBack, kTouchpad, false) == 2002,
            "non-Touchpad custom binding is preserved untouched");

    // Family-aware button display labels.
    require(std::string(input::pad_display_name(0, PadFamily::xbox)) == "A" &&
                std::string(input::pad_display_name(1, PadFamily::xbox)) == "B" &&
                std::string(input::pad_display_name(4, PadFamily::xbox)) == "View" &&
                std::string(input::pad_display_name(6, PadFamily::xbox)) == "Menu" &&
                std::string(input::pad_display_name(9, PadFamily::xbox)) == "LB" &&
                std::string(input::pad_display_name(10, PadFamily::xbox)) == "RB" &&
                std::string(input::pad_display_name(100, PadFamily::xbox)) == "LT" &&
                std::string(input::pad_display_name(101, PadFamily::xbox)) == "RT",
            "Xbox labels match expected conventions");
    require(std::string(input::pad_display_name(0, PadFamily::playstation)) == "Cross" &&
                std::string(input::pad_display_name(1, PadFamily::playstation)) == "Circle" &&
                std::string(input::pad_display_name(2, PadFamily::playstation)) == "Square" &&
                std::string(input::pad_display_name(3, PadFamily::playstation)) == "Triangle" &&
                std::string(input::pad_display_name(4, PadFamily::playstation)) == "Share" &&
                std::string(input::pad_display_name(6, PadFamily::playstation)) == "Options" &&
                std::string(input::pad_display_name(9, PadFamily::playstation)) == "L1" &&
                std::string(input::pad_display_name(10, PadFamily::playstation)) == "R1" &&
                std::string(input::pad_display_name(20, PadFamily::playstation)) == "Touchpad" &&
                std::string(input::pad_display_name(100, PadFamily::playstation)) == "L2" &&
                std::string(input::pad_display_name(101, PadFamily::playstation)) == "R2",
            "PS4/PS5 labels match PlayStation conventions");
    require(std::string(input::pad_display_name(0, PadFamily::ps3)) == "Cross" &&
                std::string(input::pad_display_name(4, PadFamily::ps3)) == "Select" &&
                std::string(input::pad_display_name(6, PadFamily::ps3)) == "Start",
            "PS3 uses Cross/Select/Start labels");
}

void test_run_toggle_policy() {
    // Toggle stays latched while moving without aiming, clears when stopping or aiming.
    require(input::update_run_toggle(true, true, false), "run toggle stays latched while moving");
    require(!input::update_run_toggle(true, false, false), "stopping movement clears run toggle");
    require(!input::update_run_toggle(true, true, true), "raising aim clears run toggle");
    require(!input::update_run_toggle(false, true, false), "off toggle stays off");

    // Hold-run OR run-toggle requests sprinting, subject to forward movement and not aiming.
    require(input::resolve_run_requested(true, false, true, false), "hold-run sprints while moving forward");
    require(input::resolve_run_requested(false, true, true, false), "run-toggle sprints while moving forward");
    require(input::resolve_run_requested(true, true, true, false), "both hold and toggle sprint");
    require(!input::resolve_run_requested(true, true, false, false), "sprinting requires forward movement");
    require(!input::resolve_run_requested(true, true, true, true), "aiming prevents sprinting");
    require(!input::resolve_run_requested(false, false, true, false), "neither hold nor toggle walks");
}

}  // namespace

int main() {
    try {
        test_keyboard_dispatch();
        test_controller_dispatch();
        test_weapon_cycling();
        test_aim_hold_sync();
        test_presets_and_classic_compatibility();
        test_playstation_support_and_capabilities();
        test_run_toggle_policy();
        std::cout << "Control dispatch, preset, PlayStation, run toggle and weapon cycling tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
