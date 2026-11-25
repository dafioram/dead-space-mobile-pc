// Port settings that are not part of the game's own save data (userdata/port_settings.ini).
#pragma once
#include <string>

namespace settings {
void load(const std::string& path);
bool loaded_from_file();  // true if path existed on disk when load() ran
bool save();  // false is logged; keep the runtime preference even if persistence fails
int get_int(const std::string& key, int def);
void set_int(const std::string& key, int value);  // saves immediately
std::string get_string(const std::string& key, const std::string& def);
void set_string(const std::string& key, const std::string& value);  // saves immediately
void erase_prefix(const std::string& prefix);  // removes matching keys and saves immediately
}  // namespace settings
