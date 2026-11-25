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

}  // namespace

int main() {
    try {
        test_keyboard_dispatch();
        test_controller_dispatch();
        test_weapon_cycling();
        std::cout << "Control dispatch and weapon cycling tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
