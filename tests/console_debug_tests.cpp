// Pure parsing/policy tests. No game, saves, assets, dumps or guest objects.
#include <iostream>
#include <limits>
#include <stdexcept>

#include "console_debug.h"

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace console_debug;
    try {
        for (auto line : {"", "/give_all", "/spawn_slasher_2", "/level_01_gov_02", "/restorex", "/creditsx_1"})
            require(parse(line).kind == Kind::none, "new parser must not intercept old commands or prefix lookalikes");

        auto preset = parse("/rich");
        require(preset.kind == Kind::rich && preset.error.empty() && preset.credits == 500000 && preset.nodes == 200,
                "rich uses the requested set-to preset");
        auto first = set_resources(preset, {75, 2});
        auto again = set_resources(preset, first);
        require(first.credits == 500000 && first.nodes == 200 && again.credits == first.credits && again.nodes == first.nodes,
                "repeating rich must set, never add to, either balance");
        auto lower = set_resources(preset, {900000, 900});
        require(lower.credits == 500000 && lower.nodes == 200, "set-to also replaces balances higher than the preset");
        auto credits = set_resources(parse("/credits_123"), {20, 7});
        require(credits.credits == 123 && credits.nodes == 7, "setting credits preserves nodes");
        auto nodes = set_resources(parse("/nodes_456"), credits);
        require(nodes.credits == 123 && nodes.nodes == 456, "setting nodes preserves credits");
        auto zero = set_resources(parse("/rich_0_0"), nodes);
        require(zero.credits == 0 && zero.nodes == 0, "zero is an intentional set-to amount");
        auto custom = parse("/rich_700000_300");
        require(custom.error.empty() && custom.credits == 700000 && custom.nodes == 300, "custom rich sets both explicit amounts");
        require(parse("/credits_2147483647").error.empty() && parse("/nodes_2147483647").error.empty(),
                "accept the documented inclusive numeric bound");
        for (auto line : {"/credits", "/credits_", "/credits_-1", "/credits_+1", "/credits_1.5", "/credits_1e3",
                          "/credits_12x", "/credits_1_2", "/credits_2147483648", "/credits_4294967295",
                          "/credits_999999999999999999999999999999", "/nodes_nan", "/nodes_-2", "/nodes_3_extra",
                          "/rich_5", "/rich_5_", "/rich_1_2_3", "/rich_1_-2", "/rich_1_2147483648", "/rich__2"}) {
            auto request = parse(line);
            require(request.kind != Kind::none && !request.error.empty(), "malformed recognized resource command must be rejected");
            auto unchanged = set_resources(request, {12, 34});
            require(unchanged.credits == 12 && unchanged.nodes == 34, "bad resource input cannot change either balance");
        }

        require(parse("/restore").error.empty() && !parse("/restore_1").error.empty(), "restore takes no numeric override");
        require(valid_restore_limits(175, 250) && valid_restore_limits(100, 0), "native suit limits need not be fixed at 100");
        float nan = std::numeric_limits<float>::quiet_NaN(), infinity = std::numeric_limits<float>::infinity();
        require(!valid_restore_limits(0, 100) && !valid_restore_limits(-1, 100) && !valid_restore_limits(100, -1) &&
                !valid_restore_limits(nan, 100) && !valid_restore_limits(100, infinity), "invalid native maxima must refuse restoration");
        require(parse("/enemies").radius == 30 && parse("/enemies_0.1").error.empty() && parse("/enemies_500").error.empty(),
                "enemy radius has documented default and inclusive bounds");
        require(parse("/enemies_12.5").radius == 12.5f, "support fractional metre radii");
        for (auto line : {"/enemies_", "/enemies_0", "/enemies_-3", "/enemies_0.09", "/enemies_501", "/enemies_nan",
                          "/enemies_inf", "/enemies_1e9999", "/enemies_10m", "/enemies_30_40"})
            require(!parse(line).error.empty(), "invalid radii must not silently fall back to an unbounded query");

        auto query = parse("/enemies");
        require(!query.mutates() && preset.mutates() && parse("/restore").mutates(), "enemy inspection is not a mutation");
        require(same_context(1, 2, 3, 1, 2, 3), "same live context can execute");
        for (bool same : {false, true}) {
            for (bool allowed : {false, true}) {
                require(execution(query, same, allowed) == (same ? Execution::ready : Execution::stale),
                        "read-only inspection needs current context but not mutation permission");
                require(execution(preset, same, allowed) == (!same ? Execution::stale : allowed ? Execution::ready : Execution::busy),
                        "mutations require both current context and safe native ownership");
            }
        }
        require(!same_context(0, 2, 3, 0, 2, 3) && !same_context(1, 0, 3, 1, 0, 3), "missing player/world is not a current context");
        require(!same_context(1, 2, 3, 4, 2, 3) && !same_context(1, 2, 3, 1, 4, 3), "replacement player/world retires a queued request");
        require(!same_context(1, 2, 3, 1, 2, 4), "even identical reused addresses cannot replay a request after native reset");

        double distance = 0;
        require(enemy_distance(0, 0, 0, 3, 4, 0, 5, distance) && distance == 5, "enemy radius includes exact 3-D boundary");
        require(!enemy_distance(0, 0, 0, 3, 4, 0, 4.9f, distance), "not an axis-aligned or horizontal-only search");
        require(enemy_distance(2, 3, 4, 2, 3, 4, 0.1f, distance) && distance == 0, "include overlapping enemies");
        require(!enemy_distance(nan, 0, 0, 0, 0, 0, 30, distance) &&
                !enemy_distance(0, 0, 0, infinity, 0, 0, 30, distance) &&
                !enemy_distance(0, 0, 0, 1, 0, 0, nan, distance), "invalid positions/radii cannot become false matches");
        require(!enemy_distance(-std::numeric_limits<float>::max(), 0, 0, std::numeric_limits<float>::max(), 0, 0, 500, distance),
                "extreme finite positions do not overflow into a nearby match");
        std::cout << "Debug console parsing, set-to semantics, context retirement and read-only radius checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
