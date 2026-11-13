// Only synthetic files are created, in an automatically removed temporary directory.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "asset_file.h"
#include "interaction_geometry.h"
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
    auto path = root / "settings.ini";
    std::ofstream(path) << "# comment\n; comment\ninvalid line\n width = 1280 \n"
                          "width = 1920\n bad_integer = invalid\n title = A=B \n\t tabs\t=\tvalue\t\n";
    settings::load(path.string());
    require(settings::get_int("width", 0) == 1920, "last duplicate setting wins");
    require(settings::get_int("missing", 42) == 42, "missing integer uses default");
    require(settings::get_int("bad_integer", 42) == 42, "malformed integer uses default");
    require(settings::get_string("title", "") == "A=B", "split only at first equals sign");
    require(settings::get_string("tabs", "") == "value", "trim tabs and whitespace");
    require(settings::get_string("missing", "fallback") == "fallback", "missing string uses default");
    settings::set_int("width", 800);
    settings::set_string("title", "persisted");
    std::ifstream saved(path);
    std::string contents((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
    require(contents.find("width = 800") != std::string::npos, "integer write persists immediately");
    require(contents.find("title = persisted") != std::string::npos, "string write persists immediately");
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
    require(near(wall.f, {0, 1, 0}) && near(wall.u, {0, 0, -1}),
            "wall free look applies the camera orbit to the complete body basis");
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

}  // namespace

int main() {
    try {
        TempDirectory temp;
        test_asset_files(temp.path);
        test_settings(temp.path);
        test_interaction_geometry();
        test_interaction_frames();
        test_oriented_zones();
        std::cout << "Asset-file, settings and interaction-geometry checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
