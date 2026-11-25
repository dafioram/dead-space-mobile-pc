// Only synthetic files are created, in an automatically removed temporary directory.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "asset_file.h"
#include "floor_snap_policy.h"
#include "interaction_geometry.h"
#include "input_policy.h"
#include "locator_input.h"
#include "menu_geometry.h"
#include "menu_widget_safety.h"
#include "menu_window_cache.h"
#include "movement_handoff.h"
#include "settings.h"

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    // Unlike assert(), checks stay active in Release/RelWithDebInfo builds.
    if (!condition) throw std::runtime_error(message);
}

struct TempDirectory {
    fs::path path;
    TempDirectory() {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; attempt++) {
            path = fs::temp_directory_path() / ("deadspace-core-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(path)) return;
        }
        throw std::runtime_error("cannot create a unique temporary directory");
    }
    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void test_asset_files(const fs::path& root) {
    fs::create_directory(root / "directory");
    require(assets::open_file((root / "directory").string()) == nullptr, "directory must not open as an asset");
    require(assets::open_file((root / "missing").string()) == nullptr, "missing asset must not open");
    std::ofstream(root / "regular.txt", std::ios::binary) << "synthetic asset";
    auto* file = assets::open_file((root / "regular.txt").string());
    require(file != nullptr, "regular asset must open");
    char data[32]{};
    auto count = std::fread(data, 1, sizeof data, file);
    std::fclose(file);
    require(std::string(data, count) == "synthetic asset", "regular asset contents must be readable");
    std::ofstream(root / "empty.txt");
    file = assets::open_file((root / "empty.txt").string());
    require(file != nullptr, "empty regular asset must open");
    std::fclose(file);
}

void test_settings(const fs::path& root) {
    auto missing_path = root / "absent.ini";
    settings::load(missing_path.string());
    require(!settings::loaded_from_file(), "loading a nonexistent settings file reports loaded_from_file() == false");
    require(!fs::exists(missing_path), "loading a nonexistent settings file must not eagerly create it");

    auto path = root / "settings.ini";
    std::ofstream(path) << "# comment\n; comment\ninvalid line\n width = 1280 \n"
                          "width = 1920\n bad_integer = invalid\n title = A=B \n\t tabs\t=\tvalue\t\n"
                          "bind_fire = Mouse Left\nbind_interact = E\npadbind_rig = Touchpad\n";
    settings::load(path.string());
    require(settings::loaded_from_file(), "loading an existing settings file reports loaded_from_file() == true");
    require(settings::get_int("width", 0) == 1920, "last duplicate setting wins");
    require(settings::get_int("missing", 42) == 42, "missing integer uses default");
    require(settings::get_int("bad_integer", 42) == 42, "malformed integer uses default");
    require(settings::get_string("title", "") == "A=B", "split only at first equals sign");
    require(settings::get_string("tabs", "") == "value", "trim tabs and whitespace");
    require(settings::get_string("missing", "fallback") == "fallback", "missing string uses default");
    settings::set_int("width", 800);
    settings::set_string("title", "persisted");
    settings::erase_prefix("bind_");
    require(settings::get_string("bind_fire", "none") == "none" &&
                settings::get_string("bind_interact", "none") == "none",
            "erase_prefix removes matching keys from memory");
    require(settings::get_string("padbind_rig", "") == "Touchpad",
            "erase_prefix leaves non-matching keys untouched");
    std::ifstream saved(path);
    std::string contents((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    require(contents.find("width = 800") != std::string::npos, "integer write persists immediately");
    require(contents.find("title = persisted") != std::string::npos, "string write persists immediately");
    require(contents.find("bind_fire") == std::string::npos && contents.find("bind_interact") == std::string::npos,
            "erase_prefix removes matching keys from disk");
    require(contents.find("padbind_rig = Touchpad") != std::string::npos,
            "erase_prefix preserves unrelated keys on disk");
}

void test_interaction_geometry() {
    // Synthetic dimensions: no fixture or coordinate comes from a game asset.
    objs::Box platform{{-1, -2, -4}, {41, 3, 4}};
    require(interact::zone_within_radius(platform, {0, 0, 0}, 13),
            "near edge of large object must remain searchable despite its distant pivot");
    require(interact::zone_within_radius(platform, {-3, 0, 0}, 2), "include a zone exactly at the search radius");
    require(!interact::zone_within_radius(platform, {-3.1f, 0, 0}, 2), "exclude zones outside the search radius");
    require(!interact::zone_within_radius(platform, {0, 20, 0}, 13), "search distance must include height");
    objs::Box corner{{3, 4, 0}, {4, 5, 1}};
    require(interact::zone_within_radius(corner, {0, 0, 0}, 5), "measure nearest corner using Euclidean distance");
    require(!interact::zone_within_radius(corner, {0, 0, 0}, 4.9f), "do not use independent per-axis radii");
    objs::Box point{{1, 2, 3}, {1, 2, 3}};
    require(interact::zone_within_radius(point, {1, 2, 3}, 0), "support point-sized zones");
    require(!interact::zone_within_radius(platform, {0, 0, 0}, -1), "reject negative search radii");
    require(!interact::zone_within_radius(objs::Box{{2, 0, 0}, {1, 1, 1}}, {0, 0, 0}, 13), "reject inverted bounds");
    float nan = std::numeric_limits<float>::quiet_NaN();
    float infinity = std::numeric_limits<float>::infinity();
    require(!interact::zone_within_radius(platform, {nan, 0, 0}, 13), "reject invalid player positions");
    require(!interact::zone_within_radius(objs::Box{{0, 0, 0}, {infinity, 1, 1}}, {0, 0, 0}, 13), "reject infinite bounds");
}

void test_interaction_frames() {
    auto near = [](objs::Vec3 a, objs::Vec3 b) {
        return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 1e-5f;
    };
    constexpr float quarter_turn = 1.57079632679f;
    for (auto frame : {
             interact::interaction_view({10, 20, 30}, {0, 0, 1}, {0, 1, 0}, 0),
             interact::interaction_view({10, 20, 30}, {0, 0, 1}, {1, 0, 0}, 0),
             interact::interaction_view({10, 20, 30}, {0, 1, 0}, {1, 0, 0}, 0),
             interact::interaction_view({10, 20, 30}, {0, 0, 1}, {0, -1, 0}, 0),
             interact::interaction_view({10, 20, 30}, {0, 0.6f, 0.8f}, {0, 0.8f, -0.6f}, 0),
             interact::interaction_view({10, 20, 30}, {0, 1, 0}, {1, 0, 0}, quarter_turn)}) {
        require(near(interact::view_local(frame, frame.p), {}), "player is at the interaction-frame origin");
        require(std::fabs(interact::dot(frame.f, frame.u)) < 1e-5f, "forward and up remain perpendicular");
        require(near(interact::cross(frame.u, frame.f), frame.r), "right follows the surface orientation");
        for (int i = 0; i < 8; ++i) {
            objs::Vec3 corner{i & 1 ? 0.7f : -0.7f, i & 2 ? 1.3f : -1.05f, i & 4 ? 1.8f : -0.3f};
            require(near(interact::view_local(frame, interact::view_world(frame, corner)), corner),
                    "drawn hitbox corners must map back to the selection volume on every surface");
        }
    }
    auto floor = interact::interaction_view({}, {0, 0, 1}, {0, 1, 0}, quarter_turn);
    require(near(floor.f, {1, 0, 0}), "floor free-look yaw preserves the original direction");
    auto wall = interact::interaction_view({}, {0, 1, 0}, {1, 0, 0}, quarter_turn);
    require(near(wall.f, {0, 0, 1}) && near(wall.u, {1, 0, 0}),
            "wall free look turns around body up rather than world Y");
    auto ceiling = interact::interaction_view({}, {0, 0, 1}, {0, -1, 0}, 0);
    require(near(interact::view_world(ceiling, {0, -1, 0}), {0, 1, 0}),
            "interaction-volume bottom points toward the ceiling when upside down");
}

void test_oriented_zones() {
    auto near = [](objs::Vec3 a, objs::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 1e-5f; };
    objs::Region marker{{10, 20, 30}, {0, 2, 0}, {-3, 0, 0}, {0, 0, 4}};
    auto frame = interact::region_frame(marker);
    require(near(frame.p, marker.p), "orientation must preserve the corrected world marker anchor");
    require(near(frame.r, {0, 1, 0}) && near(frame.u, {-1, 0, 0}) && near(frame.f, {0, 0, 1}),
            "strip model scale without discarding marker rotation");
    interact::Zone terminal{frame, {{-0.6f, -1.3f, -0.6f}, {0.6f, 0.5f, 0.6f}}};
    require(near(interact::view_world(frame, {0, -1, 0}), {11, 20, 30}), "below the marker follows terminal up");
    objs::Vec3 corners[8];
    interact::zone_corners(terminal, corners);
    for (int i = 0; i < 8; ++i) {
        auto p = interact::view_local(frame, corners[i]);
        require(near(p, {i & 1 ? 0.6f : -0.6f, i & 2 ? 0.5f : -1.3f, i & 4 ? 0.6f : -0.6f}),
                "debug corners retain the unscaled local dimensions and padding");
    }
    require(interact::zone_within_radius(terminal, {12.3f, 20, 30}, 1.001f), "radius uses the actual rotated edge");
    require(!interact::zone_within_radius(terminal, {12.3f, 20, 30}, 0.99f), "exclude points beyond rotated reach");
    auto identity = interact::world_view();
    interact::Zone cube{identity, {{-1, -1, -1}, {1, 1, 1}}};
    require(interact::zones_overlap(cube, cube), "coincident boxes overlap despite parallel cross axes");
    auto touching = cube;
    touching.frame.p = {2, 0, 0};
    require(!interact::zones_overlap(cube, touching), "face contact alone preserves strict overlap semantics");
    touching.frame.p = {1.99f, 0, 0};
    require(interact::zones_overlap(cube, touching), "a small penetration must overlap");

    // The diagonal box's enclosing AABB contains this small cube, but the drawn
    // oriented volume does not. This fails if selection uses the enclosing AABB.
    float s = std::sqrt(0.5f);
    interact::Zone diagonal{{{}, {0, 0, 1}, {s, s, 0}, {-s, s, 0}}, {{-2, -0.1f, -0.1f}, {2, 0.1f, 0.1f}}};
    interact::Zone outside{identity, {{1.2f, -1.4f, -0.05f}, {1.4f, -1.2f, 0.05f}}};
    auto enclosing = interact::zone_bounds(diagonal, identity);
    require(interact::zones_overlap({identity, enclosing}, outside), "synthetic AABB false-positive setup");
    require(!interact::zones_overlap(diagonal, outside) && !interact::zones_overlap(outside, diagonal),
            "reject empty AABB corners using both boxes' axes");
    auto inside = outside;
    inside.frame.p = {0, 2.6f, 0};
    require(interact::zones_overlap(diagonal, inside), "select points along the actual diagonal box");

    interact::Zone bar{identity, {{-2, -0.2f, -0.2f}, {2, 0.2f, 0.2f}}};
    interact::Zone skew{{{-3, 0, 0.25f}, {0.5f, -s, 0.5f}, {s, 0, -s}, {0.5f, s, 0.5f}}, bar.box};
    require(!interact::zones_overlap(bar, skew) && !interact::zones_overlap(skew, bar),
            "skew edge-edge separation requires cross-product axes, not just the six face normals");

    for (auto view : {interact::interaction_view({10, 20, 30}, {0, 1, 0}, {1, 0, 0}, 0.4f),
                      interact::interaction_view({10, 20, 30}, {0, 0.6f, 0.8f}, {0, 0.8f, -0.6f}, -0.7f)}) {
        auto transform = [&](interact::Zone zone) {
            auto rotation = view;
            rotation.p = {};
            zone.frame = {interact::view_world(view, zone.frame.p), interact::view_world(rotation, zone.frame.f),
                          interact::view_world(rotation, zone.frame.r), interact::view_world(rotation, zone.frame.u)};
            return zone;
        };
        require(!interact::zones_overlap(transform(diagonal), transform(outside)),
                "a common world rotation/translation preserves separation");
        require(interact::zones_overlap(transform(diagonal), transform(inside)),
                "a common world rotation/translation preserves overlap");
        require(!interact::zones_overlap(transform(bar), transform(skew)),
                "edge-edge separation remains valid on wall and slope frames");
    }

    marker.right = {-2, 0, 0};
    marker.up = {0, 3, 0};
    frame = interact::region_frame(marker);
    require(near(frame.r, {-1, 0, 0}), "preserve reflected model axes instead of forcing a handedness");
    for (objs::Vec3 bad : {objs::Vec3{}, objs::Vec3{0, 1, 0},
                           objs::Vec3{std::numeric_limits<float>::infinity(), 0, 0}}) {
        marker.right = bad;
        frame = interact::region_frame(marker);
        require(near(frame.p, marker.p) && near(frame.r, identity.r) && near(frame.u, identity.u) && near(frame.f, identity.f),
                "degenerate, sheared or invalid marker axes fall back without moving the anchor");
    }
}

void test_surface_camera_orbit() {
    using objs::Vec3;
    auto near = [](Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 2e-5f; };
    const float quarter_turn = 1.57079632679f;
    struct Surface { Vec3 up, forward; };
    const Surface surfaces[] = {
        {{0, 1, 0}, {0, 0, 1}}, {{0, -1, 0}, {0, 0, 1}},
        {{1, 0, 0}, {0, 1, 0}}, {{-1, 0, 0}, {0, 1, 0}},
        {{0, 0, 1}, {1, 0, 0}}, {{0, 0, -1}, {1, 0, 0}},
        {{0, 0.8f, -0.6f}, {0, 0.6f, 0.8f}},
    };
    Vec3 pivot{13, -7, 29};
    for (auto surface : surfaces) {
        auto up = surface.up, forward = surface.forward;
        auto right = interact::cross(up, forward);
        for (float angle : {-quarter_turn, -0.37f, 0.0f, 0.37f, quarter_turn}) {
            camera::YawRotation rotation(up, angle);
            Vec3 expected{forward.x * std::cos(angle) + right.x * std::sin(angle),
                          forward.y * std::cos(angle) + right.y * std::sin(angle),
                          forward.z * std::cos(angle) + right.z * std::sin(angle)};
            require(near(rotation.rotate(forward), expected), "yaw must follow the local body right on every surface");
            require(near(rotation.rotate(up), up), "yaw must preserve the surface up, including on slopes");
            Vec3 eye{pivot.x - forward.x * 3 + up.x, pivot.y - forward.y * 3 + up.y,
                     pivot.z - forward.z * 3 + up.z};
            auto moved = rotation.orbit(eye, pivot);
            Vec3 expected_eye{pivot.x - expected.x * 3 + up.x, pivot.y - expected.y * 3 + up.y,
                              pivot.z - expected.z * 3 + up.z};
            require(near(moved, expected_eye), "orbit rotates all three coordinates about the player's actual pivot");
            require(near(rotation.orbit(pivot, pivot), pivot), "yaw must never move the pivot");
            camera::YawRotation inverse(up, -angle);
            require(near(inverse.orbit(moved, pivot), eye), "camera orbit must be reversible without scale or drift");
            Vec3 camera_up{up.x + forward.x * 0.2f, up.y + forward.y * 0.2f, up.z + forward.z * 0.2f};
            auto turned_up = rotation.rotate(camera_up);
            require(std::fabs(interact::dot(turned_up, expected) - interact::dot(camera_up, forward)) < 1e-5f,
                    "the camera up vector must rotate with eye/target to preserve pitch and roll");

            auto view = interact::interaction_view(pivot, forward, up, angle);
            require(near(view.f, expected) && near(view.u, up), "selection and debug geometry follow the same camera yaw");
            // adjustYaw uses a local-Y rotation too: transferring some yaw to
            // the body and retaining the remainder must leave the view put.
            camera::YawRotation body_step(up, angle * 0.4f), remaining(up, angle * 0.6f);
            require(near(body_step.rotate(remaining.rotate(forward)), expected),
                    "stationary orbit and moving/aiming catch-up must agree on the yaw axis and sign");
            camera::YawRotation scaled({up.x * 3, up.y * 3, up.z * 3}, angle);
            require(near(scaled.rotate(forward), expected), "normalise supplied body up without scaling the view");
        }
    }
    require(near(camera::YawRotation({0, -1, 0}, quarter_turn).rotate({0, 0, 1}), {-1, 0, 0}),
            "ceiling orbit must not reuse the upright world-Y direction");
    for (Vec3 bad : {Vec3{}, Vec3{0, std::numeric_limits<float>::infinity(), 0},
                     Vec3{std::numeric_limits<float>::quiet_NaN(), 0, 0}})
        require(near(camera::YawRotation(bad, quarter_turn).orbit({1, 2, 3}, pivot), {1, 2, 3}),
                "invalid body up leaves the native camera unchanged rather than inventing an axis");
    require(near(camera::YawRotation({0, 1, 0}, std::numeric_limits<float>::quiet_NaN()).rotate({1, 2, 3}), {1, 2, 3}),
            "invalid yaw must not contaminate the native camera");
}

void test_floor_snap_recovery() {
    movement::FloorSnapRecovery recovery;
    objs::Vec3 p{};
    recovery.update(true, true, p, 20);
    for (int i = 0; i < 4; i++) recovery.update(true, true, p, 20);
    require(!recovery.active(), "a short movement hitch must keep the original floor ray");
    p.z += 0.04f;
    recovery.update(true, true, p, 20);
    require(!recovery.active(), "ordinary movement must clear accumulated stall time");
    for (int i = 0; i < 5; i++) recovery.update(true, true, p, 20);
    require(recovery.active(), "sustained blocked walking must request the wider floor probe");
    for (int i = 0; i < 19; i++) recovery.update(true, true, p, 20);
    require(recovery.active(), "give a recovery sweep time to clear the seam");
    recovery.update(true, true, p, 20);
    require(!recovery.active(), "a failed recovery must return to the original ray after a bounded interval");
    for (int i = 0; i < 10; i++) recovery.update(true, true, p, 20);
    require(!recovery.active(), "leave a ray-only cooldown between recovery attempts");
    for (int i = 0; i < 5; i++) recovery.update(true, true, p, 20);
    require(recovery.active(), "a still-blocked player may retry after cooldown");
    recovery.update(true, false, p, 20);
    require(!recovery.active(), "releasing movement must immediately cancel recovery");

    for (int dt : {8, 16, 33, 50}) {
        recovery.reset();
        p = {};
        recovery.update(true, true, p, dt);
        for (int i = 0; i < 100; i++) {
            p.z += 2.0f * float(dt) / 1000.0f;
            recovery.update(true, true, p, dt);
            require(!recovery.active(), "normal walking must stay on the ray at different frame rates");
        }
        for (int i = 0; i < (100 + dt - 1) / dt; i++) recovery.update(true, true, p, dt);
        require(recovery.active(), "stall detection must use elapsed simulation time, not a frame count");
        recovery.update(false, true, p, dt);
        require(!recovery.active(), "leaving grounded living zero-G locomotion must cancel recovery");
    }
    recovery.update(true, true, p, 20);
    for (int i = 0; i < 4; i++) recovery.update(true, true, p, 20);
    p.x += 10;
    recovery.update(true, true, p, 20);
    require(!recovery.active(), "a teleport is movement, not a stalled frame");
    recovery.update(true, true, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, 20);
    require(!recovery.active(), "invalid positions must clear recovery");
    recovery.update(true, true, p, 0);
    require(!recovery.active(), "non-advancing simulation time must not arm recovery");
    recovery.reset();
    for (int i = 0; i < 100; i++) {
        p.z += 0.004f;
        recovery.update(true, 0.1f, p, 20);
        require(!recovery.active(), "slow partial-stick movement must not be mistaken for a stall");
    }
}

void test_main_menu_bar() {
    for (const char* state : {"UI_BUTTON_MAIN_OFF", "UI_BUTTON_MAIN_ON"}) {
        const char* regular = menu_nav::main_menu_child_layout(state, false);
        const char* extras = menu_nav::main_menu_child_layout(state, true);
        require(regular && extras && std::string_view(regular) != extras,
                "Extras must not reuse Info's mutable caption/icon child layouts in either visual state");
        require(std::string_view(extras).find("_EXTRAS_") != std::string_view::npos,
                "Extras must resolve to its separately registered visual children");
    }
    require(std::string_view(menu_nav::main_menu_child_layout("UI_BUTTON_MAIN_OFF", false)) == "UI_BUTTON_MAIN_PORT_OFF" &&
                std::string_view(menu_nav::main_menu_child_layout("UI_BUTTON_MAIN_ON", false)) == "UI_BUTTON_MAIN_PORT_ON",
            "regular Info/Play/Options visual references must remain unchanged");
    require(!menu_nav::main_menu_child_layout("UI_UNRELATED", true) &&
                !menu_nav::main_menu_child_layout("UI_UNRELATED", false),
            "unrelated child layouts must not be redirected");
    for (float width : {480.0f, 512.0f, 568.8889f, 598.0f, 599.0f, 640.0f, 854.0f}) {
        auto bar = menu_nav::main_menu_bar(width);
        auto quit = bar.quit_rect(320);
        require(bar.scale > 0 && bar.scale <= 1, "bottom-bar artwork must fit without enlarging it");
        require(quit.x >= 0 && quit.x + quit.w <= width, "Quit must stay inside the screen");
        require(quit.y == 272 && quit.h == 31, "Quit must share the bottom buttons' artwork baseline and height");
        float options_right = bar.x[1] + 113 * bar.scale;
        float extras_left = bar.x[3] + 4 * bar.scale;
        float info_left = bar.x[2] + 4 * bar.scale, info_right = bar.x[2] + 113 * bar.scale;
        require(info_left >= options_right - 0.001f, "Info must fit after Options");
        require(info_right <= extras_left + 0.001f, "Info must fit before Extras");
        require(quit.x >= bar.x[3] + 113 * bar.scale - 0.001f, "Quit must fit after Extras at the far right");
        auto local = bar.quit_local_rect();
        require(std::fabs(bar.x[3] + local.x - quit.x) < 0.001f && local.y + 268 == quit.y,
                "the animated child placement and screen-space Quit hit target must agree");
        auto icon = bar.quit_icon_rect();
        require(icon.x >= 0 && icon.x + icon.w < 29 * bar.scale && icon.y >= 0 && icon.y + icon.h < quit.h,
                "the close icon must fit inside Quit's icon well");
        require(std::fabs(icon.x + icon.w * 0.5f - 14.5f * bar.scale) < 0.001f &&
                    icon.y + icon.h * 0.5f == 15.5f,
                "the close icon must use the same centred placement as neighbouring icons");
        require(icon.x + icon.w < 32 * bar.scale, "the close icon must stay clear of the left-aligned caption");
        require(quit.contains(quit.x + quit.w * 0.5f, quit.y + quit.h * 0.5f), "Quit centre must accept a click");
        require(!quit.contains(quit.x - 1, quit.y + 1), "space left of Quit must not activate it");
        require(!quit.contains(quit.x + quit.w, quit.y + 1), "Quit's right boundary must not claim its neighbour");
        require(!quit.contains(quit.x + 1, quit.y + quit.h), "below the bottom bar must not activate Quit");
        for (float pixels_per_unit : {1.0f, 1.5f, 2.25f, 4.0f}) {
            float x = (quit.x + quit.w * 0.5f) * pixels_per_unit;
            float y = (quit.y + quit.h * 0.5f) * pixels_per_unit;
            require(quit.contains(x / pixels_per_unit, y / pixels_per_unit),
                    "scaled mouse coordinates and layout geometry must agree");
        }
    }
    auto wide = menu_nav::main_menu_bar(640);
    require(wide.x[0] == 10 && wide.x[1] == 118 && wide.x[3] == 385 && wide.x[4] == 494,
            "wide layouts must preserve the established slot positions after swapping Info and Quit");
    auto narrow = menu_nav::main_menu_bar(480);
    require(narrow.scale < 1, "4:3 must compact the bar rather than hide or overlap Quit");
}

void test_death_input_ownership() {
    // All player/pause/cinematic/dialog combinations, including a dead player
    // that continues receiving updates throughout its animation/restart screen.
    for (int bits = 0; bits < 32; bits++) {
        bool current = (bits & 1) != 0, alive = (bits & 2) != 0, paused = (bits & 4) != 0,
             cinematic = (bits & 8) != 0, dialog = (bits & 16) != 0;
        bool gameplay = input::gameplay_input_allowed(current, alive, paused, cinematic, dialog);
        require(gameplay == (bits == 3), "only a current living player outside menus/cinematics owns gameplay input");
        for (int capture = 0; capture < 8; capture++) {
            bool alt = (capture & 1) != 0, focused = (capture & 2) != 0, scripted = (capture & 4) != 0;
            bool expected = bits == 3 && !alt && (focused || scripted);
            require(input::should_capture_gameplay(gameplay, alt, focused, scripted) == expected,
                    "focus, scripted input and Left Alt must not bypass death/menu input ownership");
        }
    }
    require(input::gameplay_input_allowed(true, true, false, false, false), "ordinary living gameplay remains enabled");
    require(!input::gameplay_input_allowed(true, false, false, false, false),
            "death releases capture even before the HUD reports pause or a recognised dialog");
    require(!input::gameplay_input_allowed(false, false, false, false, false), "a missing player cannot recapture the cursor");
    require(input::gameplay_input_allowed(true, true, false, false, false), "a live respawn may recapture input normally");
}

struct FakeWidgetMemory {
    std::map<uint32_t, uint32_t> words;
    std::map<uint32_t, uint8_t> bytes;
    std::set<uint32_t> pages;
    int reads = 0;
    bool readable(uint32_t address, size_t size) const {
        return menu_nav::readable_guest_range(address, size, [&](uint32_t page) { return pages.count(page) != 0; });
    }
    uint32_t read32(uint32_t address) {
        require(readable(address, 4) && words.count(address), "guard must not read an unmapped or uninitialised word");
        reads++;
        return words.at(address);
    }
    uint8_t read8(uint32_t address) {
        require(readable(address, 1) && bytes.count(address), "guard must not read an unmapped or uninitialised byte");
        reads++;
        return bytes.at(address);
    }
};

void test_menu_widget_safety() {
    // Entirely synthetic addresses/fields: no library, object dump or save is used.
    constexpr uint32_t button = 0x30000, sub = 0x50000, entity = 0x70000;
    FakeWidgetMemory good;
    good.pages = {0x30000, 0x40000, 0x50000, 0x60000, 0x70000, 0x80000};
    good.words = {{button, 0x40000}, {button + 0x38, sub}, {button + 0x58, entity},
                  {sub, 0x60000}, {entity, 0x80000}};
    good.bytes = {{button + 0x24, 1}, {sub + 0x54, 1}};
    using State = menu_nav::ButtonQueryState;
    require(menu_nav::button_query_state(button, good) == State::ready, "enabled visible attached button may query geometry");

    auto disabled = good;
    disabled.bytes[button + 0x24] = 0;
    disabled.words.erase(button + 0x38);
    disabled.words.erase(button + 0x58);
    require(menu_nav::button_query_state(button, disabled) == State::inactive,
            "disabled widgets must be skipped before reading their detached references");
    auto hidden = good;
    hidden.bytes[sub + 0x54] = 0;
    hidden.words.erase(button + 0x58);
    require(menu_nav::button_query_state(button, hidden) == State::inactive,
            "hidden layouts must be skipped before querying their active-region entity");
    for (uint32_t bad : {0u, 0x40u, 0x90000u}) {
        auto memory = good;
        memory.reads = 0;
        require(menu_nav::button_query_state(bad, memory) == State::discard, "reject null, low and unmapped widget addresses");
        require(memory.reads == 0, "invalid widget addresses must be rejected without dereferencing them");
        for (uint32_t field : {button, button + 0x38, button + 0x58, sub, entity}) {
            memory = good;
            memory.words[field] = bad;
            require(menu_nav::button_query_state(button, memory) == State::discard,
                    "missing or unmapped layouts, entities and vtables must not reach geometry queries");
        }
    }
    auto split = good;
    split.reads = 0;
    require(menu_nav::button_query_state(button + 0xfc0, split) == State::discard && split.reads == 0,
            "a widget crossing into an unmapped page must be rejected before its first read");
    split = good;
    split.words[entity] = 0x80ff0;
    require(menu_nav::button_query_state(button, split) == State::discard,
            "the active-region entity's virtual-method slot must fit in mapped memory too");

    int checks = 0;
    auto mapped = [&](uint32_t) { checks++; return true; };
    require(!menu_nav::readable_guest_range(0xfffffff8u, 16, mapped), "reject overflowing guest spans");
    require(!menu_nav::readable_guest_range(button, 0, mapped), "reject empty spans");
    require(checks == 0, "invalid spans must not query the mapping table");
    require(menu_nav::readable_guest_range(0xfffffffcu, 4, mapped) && checks == 1,
            "the last guest page must not overflow the page iteration");
    checks = 0;
    require(menu_nav::readable_guest_range(0x30fffu, 4098, mapped) && checks == 3,
            "validate every page, including intermediate pages, of a guest span");
}

void test_menu_window_cache() {
    using menu_nav::WindowCache;
    using menu_nav::WindowRef;
    using menu_nav::WindowView;
    // Synthetic host-only snapshots: no guest code, assets or dump fixtures.
    WindowCache cache;
    require(cache.observe(0, 10) == WindowRef{}, "null window has no lifetime");
    auto store = cache.observe(0x30000, 10);
    require(!cache.find(store, 10), "a panel event without a native draw cannot supply geometry");
    WindowView drawn{store, 10, {-12, 20, 400, 240}, 0, -30, 0x40000, 0x50000};
    require(cache.capture(drawn), "capture the real native draw's rectangle and owner");
    require(cache.find(store, 10)->rect == drawn.rect && cache.find(store, 10)->scroll_y == -30,
            "retain native clipping and signed scroll without recomputing guest geometry");
    require(cache.observe(store.address, 10) == store, "draw-before-first-panel-event keeps the same owner");
    require(cache.find(store, 11) && !cache.find(store, 12) && !cache.find(store, 9),
            "only current/previous draw frames may participate in navigation");

    auto bench = cache.observe(0x60000, 10);
    WindowView across{bench, 10, {5, 6, 270, 80}, -138, std::numeric_limits<int32_t>::min(), 0x70000, 0x80000};
    require(cache.capture(across), "horizontal lists preserve the native inactive-axis sentinel");
    require(cache.find(store, 10)->rect != cache.find(bench, 10)->rect,
            "simultaneous/nested windows do not share one global item viewport");
    require(!cache.find({store.address, bench.generation}, 10), "a generation from another owner is not valid");

    cache.retire(store.address);
    require(!cache.find(store, 10), "destruction retires geometry immediately within the same frame");
    require(!cache.capture(drawn), "a late old draw cannot resurrect a destroyed owner");
    auto replacement = cache.observe(store.address, 10);
    require(replacement != store && !cache.find(replacement, 10),
            "reused mapped window address starts a distinct lifetime with no inherited geometry");
    drawn.owner = replacement;
    require(cache.capture(drawn) && !cache.find(store, 10) && cache.find(replacement, 10),
            "new item records cannot make old references target the reused window");

    for (int field = 0; field < 4; ++field) {
        for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            WindowView invalid = drawn;
            invalid.rect[field] = bad;
            require(!cache.capture(invalid) && !cache.find(replacement, 10),
                    "invalid native geometry invalidates the previous snapshot instead of fabricating a rectangle");
            require(cache.capture(drawn), "a subsequent valid draw restores navigation");
        }
    }
    for (int dimension : {2, 3}) {
        auto invalid = drawn;
        invalid.rect[dimension] = 0;
        require(!cache.capture(invalid), "not-yet-visible zero-sized windows supply no item geometry");
        invalid.rect[dimension] = -1;
        require(!cache.capture(invalid), "negative native dimensions are not a usable viewport");
    }
    auto detached = drawn;
    detached.panel = 0;
    require(!cache.capture(detached), "detached native panels are not navigable");
    detached = drawn;
    detached.vtable = 0;
    require(!cache.capture(detached), "missing native object identity is not navigable");
    require(cache.capture(drawn), "reattached native window can be drawn again");

    cache.prune(13);
    require(!cache.find(replacement, 13) && !cache.find(bench, 13), "expired screens lose their cached owners");
    require(cache.observe(store.address, 13) != replacement, "a reobserved expired screen gets a fresh generation");
    for (uint64_t frame = 20; frame < 200; ++frame) {
        auto owner = cache.observe(0x90000, frame);
        WindowView view{owner, frame, {0, 0, 300, 200}, 0, 0, 0xa0000, 0xb0000};
        require(cache.capture(view) && cache.find(owner, frame), "repeated store entries can acquire native snapshots");
        cache.retire(owner.address);
        require(!cache.find(owner, frame), "every repeated exit retires the previous instance");
    }
}

void test_scripted_input_ownership() {
    using input::HudInputRoute;
    const int player = int(HudInputRoute::player);
    const int grapple = int(HudInputRoute::grapple);
    const int object = int(HudInputRoute::object);
    const int hidden = int(HudInputRoute::hidden);
    for (bool passthrough : {false, true}) {
        require(input::hud_routes_player_input(player, passthrough), "ordinary HUD routes to the player");
        require(input::hud_routes_player_input(grapple, passthrough), "grapple struggle controls must remain available");
        require(!input::hud_routes_player_input(object, passthrough), "object input ownership cannot be bypassed by port controls");
        require(input::hud_routes_player_input(hidden, passthrough) == passthrough,
                "hideSoftly permits gameplay; hide without passthrough does not");
        for (int unknown : {-1, 4, 100})
            require(!input::hud_routes_player_input(unknown, passthrough), "unknown native routes must not enable gameplay");
    }
    for (int state = 19; state <= 24; state++) {
        require(!input::scripted_input_allowed(player, true, state, 0),
                "bench/store entry, exit and suit-change states retain control even after UI release");
        require(!input::scripted_input_allowed(hidden, true, state, 0),
                "softly hidden HUD cannot prematurely end a store animation lock");
    }
    for (int state : {0, 4, 9, 10, 11, 18, 25})
        require(input::scripted_input_allowed(player, false, state, 0),
                "do not blanket-block aiming, grapples, zero-G jumps, kinesis or unrelated states");
    require(input::scripted_input_allowed(grapple, false, 9, 0), "native grapple route must accept struggle actions");
    require(!input::scripted_input_allowed(player, false, 0, 3), "native facing-and-position freeze owns player input");
    require(!input::scripted_input_allowed(player, false, 0, 0x43), "other freeze bits cannot mask the full control lock");
    require(!input::scripted_input_allowed(player, false, 0, 0xffffffffu), "Brute intro freezes all native control bits");
    require(input::scripted_input_allowed(player, false, 0, 1), "facing-only lock must not block movement wholesale");
    require(input::scripted_input_allowed(player, false, 0, 2), "position-only lock must not block look wholesale");
    require(input::scripted_pointer_blocked(player, 21, 0), "store exit clicks must not fall back to mobile touch movement");
    require(input::scripted_pointer_blocked(player, 0, 0xffffffffu), "Brute reveal must not receive fallback touch gestures");
    require(!input::scripted_pointer_blocked(object, 20, 0), "store and bench menus must still receive real pointer clicks");
    require(!input::scripted_pointer_blocked(player, 0, 0), "ordinary Alt touch-pointer access remains available");
    require(!input::scripted_pointer_blocked(grapple, 9, 0), "grapple input must not be mistaken for a scripted pointer lock");

    for (bool owned : {false, true})
        for (bool facing : {false, true})
            require(input::free_look_input_allowed(owned, facing) == (owned && facing),
                    "camera orbit and player catch-up must obey ownership and native canChangeFacing");

    // Model a local event drain: interact hands input away before a queued look
    // or fire; an entire enter/exit may also reset native input between frames.
    uint32_t epoch = 7;
    require(input::queued_gameplay_input_allowed(true, 7, epoch), "fresh gameplay input is delivered");
    require(!input::queued_gameplay_input_allowed(false, 7, epoch), "mid-drain script takeover drops remaining events");
    epoch++;
    require(!input::queued_gameplay_input_allowed(true, 7, epoch), "ownership restoration cannot replay pre-reset input");
    require(input::queued_gameplay_input_allowed(true, 8, epoch), "new input resumes after the native handoff");
    require(!input::queued_gameplay_input_allowed(true, 0xffffffffu, 0), "epoch wrap must not accept an old event");
}

void test_direct_locator() {
    struct Hud {
        bool gameplay = true, rig = false, valid = true, modal = false;
        bool open_allowed = true, marker_allowed = true, takeover_on_open = false, takeover_on_marker = false;
        unsigned epoch = 0;
        std::string calls;
        bool owns_gameplay() const { return valid && !modal && gameplay; }
        bool owns_rig() const { return valid && !modal && rig; }
        void rig_button() {
            calls += 'R';
            if (!rig && !open_allowed) return;
            rig = !rig;
            gameplay = !rig;
            ++epoch;  // handoffs still invalidate ordinary queued gameplay
            if (takeover_on_open) { modal = true; gameplay = rig = false; }
        }
        void marker_button() {
            require(owns_rig(), "native marker requests must target the actual RIG context");
            calls += 'M';
            require(!input::queued_gameplay_input_allowed(owns_gameplay(), 0, epoch),
                    "the ordinary second queued event is still correctly forbidden after opening RIG");
            if (marker_allowed) { rig = false; gameplay = true; }
            if (takeover_on_marker) { modal = true; gameplay = rig = false; }
        }
    };
    Hud hud;
    require(input::request_locator(hud) && hud.calls == "RM" && !hud.rig,
            "one locator request must reach the native marker despite opening RIG revoking gameplay ownership");
    require(hud.gameplay && hud.epoch == 1, "successful native activation closes its panel without another toggle");
    hud.calls.clear();
    require(input::request_locator(hud) && hud.calls == "RM", "a later fresh locator press is an independent request");

    for (bool valid : {false, true})
        for (bool modal : {false, true}) {
            Hud blocked;
            blocked.valid = valid;
            blocked.modal = modal;
            blocked.gameplay = false;
            require(!input::request_locator(blocked) && blocked.calls.empty(),
                    "pause/dialog/death/cinematic/foreign owner contexts must not open or target RIG");
        }
    Hud refused_open;
    refused_open.open_allowed = false;
    require(input::request_locator(refused_open) && refused_open.calls == "R" && !refused_open.rig,
            "a native refusal to open RIG must not emit marker input or force HUD state");
    Hud refused_marker;
    refused_marker.marker_allowed = false;
    require(input::request_locator(refused_marker) && refused_marker.calls == "RMR" &&
                !refused_marker.rig && refused_marker.gameplay,
            "a disabled/busy/no-route locator must not strand a panel opened by the shortcut");

    Hud already_open;
    already_open.gameplay = false;
    already_open.rig = true;
    require(input::request_locator(already_open) && already_open.calls == "M" && !already_open.rig,
            "a locator binding in an already-open RIG must activate its marker, not toggle the panel first");
    Hud already_open_refused;
    already_open_refused.gameplay = false;
    already_open_refused.rig = true;
    already_open_refused.marker_allowed = false;
    require(input::request_locator(already_open_refused) && already_open_refused.calls == "M" && already_open_refused.rig,
            "a native marker refusal must leave a manually opened RIG alone");

    Hud replaced;
    replaced.takeover_on_open = true;
    require(input::request_locator(replaced) && replaced.calls == "R" && replaced.modal,
            "a foreign takeover during opening must prevent marker input and cleanup targeting the wrong UI");
    Hud replaced_after_marker;
    replaced_after_marker.marker_allowed = false;
    replaced_after_marker.takeover_on_marker = true;
    require(input::request_locator(replaced_after_marker) && replaced_after_marker.calls == "RM" && replaced_after_marker.modal,
            "cleanup must not toggle a different owner that appeared during marker handling");
}

void test_movement_handoff() {
    using input::MovementHandoff;
    constexpr std::uint32_t player = 0x10000, other = 0x20000;
    MovementHandoff handoff;
    handoff.set_player(player);
    int stops = 0;
    float native_x = 0, native_y = 0;
    bool native_run = false;
    auto stop = [&] { ++stops; native_x = native_y = 0; native_run = false; return true; };

    require(!handoff.release(player, true, stop) && stops == 0, "never stop movement the port has not delivered");
    handoff.delivered(player, true, false);
    require(!handoff.active(player), "a queued/rejected movement command is not a native movement latch");
    for (int route : {0, 2, 3}) {
        for (auto vector : {std::pair{0.0f, 1.0f}, std::pair{0.0f, -1.0f},
                            std::pair{-1.0f, 0.0f}, std::pair{0.7f, 0.7f}}) {
            native_x = vector.first;
            native_y = vector.second;
            native_run = true;
            handoff.delivered(player, true, true);
            // The old stop used the same rejected queue as new gameplay. Cleanup
            // must succeed even after a RIG/object handoff or epoch invalidation.
            require(!input::queued_gameplay_input_allowed(false, 7, 8), "new gameplay remains blocked in menus");
            const bool eligible = input::movement_release_allowed(true, true, true, 0, route, false);
            require(handoff.release(player, eligible, stop), "cancel delivered movement despite menu ownership");
            require(native_x == 0 && native_y == 0 && !native_run && !handoff.active(player),
                    "walk, strafe and sprint intent must all be neutral after the handoff");
            const int before = stops;
            require(!handoff.release(player, eligible, stop) && stops == before, "cancel once, not every menu frame");
            handoff.delivered(player, true, input::queued_gameplay_input_allowed(false, 7, 8));
            require(!handoff.active(player), "a stale blocked move cannot rearm delivery state after cancellation");
            // Physical controls are separate: a key/stick released in the menu
            // sends no new motion; a still-held control can resume after return.
            require(!handoff.active(player), "releasing controls in a menu must not resurrect old movement");
            handoff.delivered(player, true, input::queued_gameplay_input_allowed(true, 8, 8));
            require(handoff.active(player), "fresh polled held input can resume after gameplay ownership returns");
            handoff.delivered(player, false, true);
            require(!handoff.active(player), "an ordinary accepted zero command also clears delivery state");
        }
    }

    for (int state = 0; state < 32; ++state)
        for (int route = -1; route < 4; ++route)
            for (int flags = 0; flags < 16; ++flags) {
                const bool current = flags & 1, alive = flags & 2, can_move = flags & 4, grabbed = flags & 8;
                const bool expected = current && alive && can_move && !grabbed && route != 1 &&
                                      state != 9 && state != 10 && state != 12;
                require(input::movement_release_allowed(current, alive, can_move, state, route, grabbed) == expected,
                        "release eligibility must preserve native grapple/swarm/shake-off/freeze/death restrictions");
            }
    for (int state : {9, 10, 12, 29}) {
        handoff.delivered(player, true, true);
        const int before = stops;
        require(!handoff.release(player, input::movement_release_allowed(true, true, false, state, 0, false), stop) &&
                    !handoff.active(player) && stops == before,
                "native animation ownership retires our latch without sending forced movement or a late stop");
    }
    handoff.delivered(player, true, true);
    require(!handoff.release(player, true, [] { return false; }) && handoff.active(player),
            "a rejected native stop must not be falsely recorded as accepted");
    require(handoff.release(player, true, stop), "a rejected stop can be retried at the next valid boundary");

    handoff.delivered(player, true, true);
    handoff.native_movement(other);
    require(handoff.active(player) && !handoff.release(other, true, stop), "other players do not own this movement latch");
    handoff.native_movement(player);
    require(!handoff.release(player, true, stop), "never cancel movement replaced by a native script/mobile event");
    handoff.delivered(player, true, true);
    handoff.set_player(0);
    handoff.set_player(player);
    require(!handoff.active(player), "a destroyed/recreated player at the same address must not inherit a stop");
    handoff.delivered(player, true, true);
    handoff.set_player(other);
    require(!handoff.release(player, true, stop) && !handoff.active(other), "replacement players start without old intent");

    handoff.set_player(player);
    handoff.delivered(player, true, true);
    require(handoff.release(player, true, [&] {
        require(!handoff.release(player, true, stop), "native cancellation cannot recursively cancel itself");
        return true;
    }), "a re-entrant observation still completes the original release");
    handoff.delivered(player, true, true);
    handoff.release(player, true, [&] {
        handoff.native_movement(player);
        return false;
    });
    require(!handoff.active(player), "a failed callback must not rearm a latch superseded by native input");
    handoff.delivered(player, true, true);
    handoff.release(player, true, [&] {
        handoff.set_player(other);
        return false;
    });
    require(!handoff.active(player) && !handoff.active(other), "a failed callback cannot rearm destroyed-player state");
}

void test_noninteractive_scripted_input() {
    using input::HudInputRoute;
    for (bool cinematic : {false, true})
        for (bool grabbed : {false, true})
            for (int route = 0; route < 4; ++route)
                for (int state = 0; state < 32; ++state)
                    for (unsigned freeze : {0u, 1u, 2u, 3u, 0x43u, 0xffffffffu}) {
                        const bool blocked = input::noninteractive_scripted_input(cinematic, state, route, grabbed, freeze);
                        const bool interactive = grabbed || route == int(HudInputRoute::grapple) ||
                                                 state == 9 || state == 10 || state == 12 ||
                                                  (route == int(HudInputRoute::object) &&
                                                   ((state >= 19 && state <= 24) ||
                                                    (!cinematic && state != 27 && state != 29 && state != 30)));
                        const bool locked = state == 1 || (state >= 19 && state <= 24) ||
                                            state == 27 || state == 29 || state == 30 || (freeze & 3) == 3;
                        require(blocked == (!interactive && (cinematic || locked)),
                                "only known noninteractive scripted sequences block input");
                        if (interactive)
                            require(!blocked, "grapples, swarms, shake-off and object-owned menus must remain interactive");
                        require(input::cinematic_skip_allowed(cinematic, blocked) == (cinematic && blocked),
                                "only a tracked noninteractive movie may consume an existing skip binding");
                    }
    require(input::noninteractive_scripted_input(false, 1, 0, false, 0),
            "boss camera frozen-idle must block without a cinematic update or explicit freeze flags");
    require(!input::noninteractive_scripted_input(false, 0, 0, false, 0),
            "endIdleFrozen must restore ordinary boss combat input immediately");
    for (int state : {19, 20, 21, 22, 23, 24, 27, 29, 30})
        require(input::noninteractive_scripted_input(false, state, 0, false, 0),
                "known store/suit/tram/generic/weapon-collection animations are noninteractive");
    require(input::noninteractive_scripted_input(false, 29, int(HudInputRoute::player), false, 0),
            "Cinematic2 generic animation must block even without a GameObjectCinematic update");
    require(input::noninteractive_scripted_input(false, 29, int(HudInputRoute::object), false, 0),
            "noninteractive animation must not become mobile touch input through an object-owned route");
    for (int combat_state : {0, 4, 6, 8, 11, 13, 14, 15, 16, 17, 18, 25, 26, 28, 31})
        require(!input::noninteractive_scripted_input(false, combat_state, int(HudInputRoute::player), false, 0),
                "ordinary combat/aiming/zero-G/kinesis and unknown states must not become scripted locks");
    for (unsigned partial : {1u, 2u})
        require(!input::noninteractive_scripted_input(false, 0, 0, false, partial),
                "partial facing/position freezes must not turn ordinary gameplay into a movie");
    require(input::noninteractive_scripted_input(false, 0, 0, false, 3),
            "full native freeze covers scripted reveals without a special player state");
    for (int interactive_state : {9, 10, 12})
        require(!input::noninteractive_scripted_input(true, interactive_state, 0, false, 0xffffffffu),
                "native grapple/swarm/shake-off states outrank cinematic/freeze signals in the new guard");
    for (int menu_state = 19; menu_state <= 24; ++menu_state)
        require(!input::noninteractive_scripted_input(true, menu_state, int(HudInputRoute::object), false, 3),
                "object-owned store/bench UI must not depend on a recent menu heartbeat to accept clicks");
    for (int body_state : {0, 1})
        require(!input::noninteractive_scripted_input(false, body_state, int(HudInputRoute::object), false, 3),
                "a frozen player must not block an interactive object's pointer");
    for (int animation_state : {27, 29, 30})
        require(input::noninteractive_scripted_input(false, animation_state, int(HudInputRoute::object), false, 0),
                "object routing alone must not reopen an explicitly noninteractive animation");
    require(input::noninteractive_scripted_input(true, 1, int(HudInputRoute::object), false, 3),
            "object routing alone must not reopen a live movie");
    require(!input::cinematic_skip_allowed(false, true),
            "boss camera, tram and pickup locks must not introduce a new cinematic skip");
    require(input::recent_cinematic(true, 100, 100), "a native update is visible before the next host frame");
    require(input::recent_cinematic(true, 299, 100), "recent camera-only cinematics retain ownership");
    require(!input::recent_cinematic(true, 300, 100), "stale cinematic signal expires");
    require(!input::recent_cinematic(false, 100, 100), "a finished cinematic cannot retain control");
    require(input::recent_cinematic(true, 20, 0xfffffff0u), "cinematic expiry handles tick wrap");

    // A queued click before frozen-idle must be discarded, not delivered
    // after the cutscene. The existing reset epoch still handles restoration.
    const bool blocked = input::noninteractive_scripted_input(false, 1, 0, false, 0);
    require(!input::queued_gameplay_input_allowed(!blocked, 5, 5), "queued gameplay cannot enter a scripted camera handoff");
    require(!input::queued_gameplay_input_allowed(true, 5, 6), "old clicks cannot replay after a native reset");
    require(input::queued_gameplay_input_allowed(true, 6, 6), "fresh input resumes after the cutscene");
}

}  // namespace

int main() {
    try {
        TempDirectory temp;
        test_asset_files(temp.path);
        test_settings(temp.path);
        test_interaction_geometry();
        test_interaction_frames();
        test_oriented_zones();
        test_surface_camera_orbit();
        test_floor_snap_recovery();
        test_main_menu_bar();
        test_death_input_ownership();
        test_scripted_input_ownership();
        test_direct_locator();
        test_movement_handoff();
        test_noninteractive_scripted_input();
        test_menu_widget_safety();
        test_menu_window_cache();
        std::cout << "Asset-file, settings, geometry, widget safety, input ownership and floor-snap policy checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
