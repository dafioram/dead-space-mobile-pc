// Only synthetic files are created, in an automatically removed temporary directory.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "asset_file.h"
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

}  // namespace

int main() {
    try {
        TempDirectory temp;
        test_asset_files(temp.path);
        test_settings(temp.path);
        std::cout << "Asset-file and settings checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
