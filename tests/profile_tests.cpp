#include "temporary_profile.h"
#include "settings.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path root;
    Fixture() {
        std::random_device random;
        for (int i = 0; i < 32; i++) {
            auto candidate = fs::temp_directory_path() / ("deadspace-profile-test-" + std::to_string(random()));
            if (fs::create_directory(candidate)) { root = candidate; break; }
        }
        if (root.empty()) throw std::runtime_error("cannot create synthetic fixture");
        fs::create_directory(root / "scratch");
    }
    ~Fixture() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void write(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(value.data(), std::streamsize(value.size()));
    if (!out) throw std::runtime_error("cannot write synthetic fixture");
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read synthetic fixture");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void test_copy_and_writes() {
    Fixture f;
    auto source = f.root / "original userdata";
    const fs::path save = "data/data/synthetic.game/profile.sav";
    const std::string original("synthetic\0save", 14);
    write(source / save, original);
    write(source / "port_settings.ini", "free_look = 1\n");
    fs::create_directory(source / "empty");
    fs::permissions(source / save, fs::perms::owner_write, fs::perm_options::remove);
    auto save_time = fs::last_write_time(source / save);
    {
        TemporaryProfile session;
        session.prepare(source, f.root / "scratch");
        auto copy = session.data_path();
        require(copy != source && read(copy / save) == original, "copy must load the entire nested original save tree");
        require(fs::is_directory(copy / "empty"), "preserve empty directories");
        write(copy / save, "temporary checkpoint");
        fs::rename(copy / save, copy / "renamed.sav");
        fs::remove(copy / "renamed.sav");
        write(copy / "new-profile.sav", "temporary profile");
        settings::load((copy / "port_settings.ini").string());
        require(settings::get_int("free_look", 0) == 1, "load copied port settings");
        settings::set_int("free_look", 0);
        require(read(source / save) == original, "save writes, rename and delete must never affect the original");
        require(fs::last_write_time(source / save) == save_time, "preparing and writing the session must not rewrite the source timestamp");
        require(read(source / "port_settings.ini") == "free_look = 1\n", "production settings writes must use the temporary copy");
        require(!fs::exists(source / "new-profile.sav"), "new profiles must stay temporary");
        require(read(copy / "port_settings.ini").find("free_look = 0") != std::string::npos, "settings should work within the session");
    }
    require(fs::is_empty(f.root / "scratch"), "normal destruction must remove only the private session");
    fs::permissions(source / save, fs::perms::owner_write, fs::perm_options::add);
    TemporaryProfile again;
    again.prepare(source, f.root / "scratch");
    require(read(again.data_path() / save) == original, "relaunch starts from the original checkpoint again");
    require(read(again.data_path() / "port_settings.ini") == "free_look = 1\n", "relaunch restores original settings too");
    require(!again.discard(), "explicit cleanup succeeds before an _Exit-style shutdown");
    require(again.data_path().empty() && fs::is_empty(f.root / "scratch"), "explicit cleanup retires its owned path");
}

void test_missing_and_concurrent_profiles() {
    Fixture f;
    auto source = f.root / "missing";
    TemporaryProfile a, b;
    a.prepare(source, f.root / "scratch");
    b.prepare(source, f.root / "scratch");
    require(!fs::exists(source), "do not create a missing original root");
    require(a.data_path() != b.data_path(), "simultaneous sessions must never share a scratch directory");
    write(a.data_path() / "save", "a");
    bool rejected = false;
    try { a.prepare(source, f.root / "scratch"); } catch (const std::logic_error&) { rejected = true; }
    require(rejected && read(a.data_path() / "save") == "a", "preparing twice must not discard or reuse an active profile");
    require(!fs::exists(b.data_path() / "save"), "simultaneous sessions are isolated");
    auto b_path = b.data_path();
    require(!a.discard() && fs::exists(b_path), "cleanup cannot remove a different session");
}

void test_fail_closed() {
    Fixture f;
    auto source = f.root / "original";
    write(source / "save", "original");
    auto rejects = [&](const fs::path& from, const fs::path& parent) {
        TemporaryProfile session;
        bool rejected = false;
        try { session.prepare(from, parent); } catch (const std::exception&) { rejected = true; }
        require(rejected && session.data_path().empty(), "invalid copies must fail closed, not fall back to the original");
        require(read(source / "save") == "original", "failed preparation must not alter the source");
    };
    rejects(source / "save", f.root / "scratch");
    rejects(source, source);
    rejects(source, f.root / "missing-parent");
    require(fs::is_empty(f.root / "scratch"), "failed preparation leaves no partial copied profile");
    std::error_code error;
    fs::create_symlink(source / "save", source / "linked-save", error);
    if (!error) {
        rejects(source, f.root / "scratch");
        require(fs::is_empty(f.root / "scratch"), "rejecting a linked entry cleans up a partial copy");
        fs::remove(source / "linked-save");
    } else {
        std::cout << "Symlink creation unavailable; link fixture skipped.\n";
    }
    error.clear();
    fs::create_directory_symlink(source, f.root / "linked-root", error);
    if (!error) rejects(f.root / "linked-root", f.root / "scratch");
    error.clear();
    fs::create_hard_link(source / "save", source / "hard-linked-save", error);
    if (!error) {
        TemporaryProfile session;
        session.prepare(source, f.root / "scratch");
        write(session.data_path() / "hard-linked-save", "changed");
        require(read(source / "save") == "original" && read(session.data_path() / "save") == "original",
                "copy bytes rather than preserving hard links to originals or sibling save files");
    }
}

}  // namespace

int main() {
    try {
        test_copy_and_writes();
        test_missing_and_concurrent_profiles();
        test_fail_closed();
        std::cout << "Synthetic temporary-profile protection checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
