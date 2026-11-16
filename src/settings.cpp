#include "settings.h"

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#include "log.h"
#include "save_io.h"

namespace settings {

namespace {
std::mutex g_lock;
std::string g_path;
std::map<std::string, std::string> g_values;
}  // namespace

void load(const std::string& path) {
    std::lock_guard lk(g_lock);
    g_path = path;
    g_values.clear();
    std::ifstream f(save_io::utf8_path(path));
    if (!f) {
        std::error_code error;
        bool exists = std::filesystem::exists(save_io::utf8_path(path), error);
        if (!exists && !error) LOGI("[save-io] port settings absent; using defaults path=%s", path.c_str());
        else LOGE("[save-io] port settings read open FAILED path=%s reason=%s", path.c_str(), error ? error.message().c_str() : "unreadable file");
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t')) s.pop_back();
            while (!s.empty() && (s[0] == ' ' || s[0] == '\t')) s.erase(0, 1);
            return s;
        };
        g_values[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    if (f.bad()) LOGE("[save-io] port settings read FAILED path=%s; preferences may be incomplete", path.c_str());
}

bool save() {
    std::lock_guard lk(g_lock);
    if (g_path.empty()) return true;
    std::ostringstream f;
    f << "# Dead Space PC port settings\n";
    for (auto& [k, v] : g_values) f << k << " = " << v << "\n";
    return save_io::atomic_text(save_io::utf8_path(g_path), f.str());
}

int get_int(const std::string& key, int def) {
    std::lock_guard lk(g_lock);
    auto it = g_values.find(key);
    if (it == g_values.end()) return def;
    try {
        return std::stoi(it->second);
    } catch (...) {
        return def;
    }
}

void set_int(const std::string& key, int value) {
    {
        std::lock_guard lk(g_lock);
        g_values[key] = std::to_string(value);
    }
    save();
}

std::string get_string(const std::string& key, const std::string& def) {
    std::lock_guard lk(g_lock);
    auto it = g_values.find(key);
    return it == g_values.end() ? def : it->second;
}

void set_string(const std::string& key, const std::string& value) {
    {
        std::lock_guard lk(g_lock);
        g_values[key] = value;
    }
    save();
}

}  // namespace settings
