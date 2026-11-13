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
#include "floor_snap_policy.h"
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
    require(!interact::zone_within_radius({{2, 0, 0}, {1, 1, 1}}, {0, 0, 0}, 13), "reject inverted bounds");
    float nan = std::numeric_limits<float>::quiet_NaN();
    float infinity = std::numeric_limits<float>::infinity();
    require(!interact::zone_within_radius(platform, {nan, 0, 0}, 13), "reject invalid player positions");
    require(!interact::zone_within_radius({{0, 0, 0}, {infinity, 1, 1}}, {0, 0, 0}, 13), "reject infinite bounds");
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

}  // namespace

int main() {
    try {
        TempDirectory temp;
        test_asset_files(temp.path);
        test_settings(temp.path);
        test_interaction_geometry();
        test_floor_snap_recovery();
        std::cout << "Asset-file, settings, interaction-geometry and floor-snap policy checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
