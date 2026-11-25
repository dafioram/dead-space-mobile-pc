// Parser, serializer, and dialogue-chain repair for published/sounds/soundBase.sb.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sound_base {

// SoundsBase::Init() in libDeadSpace.so preallocates 0x1cc (460) SoundObject slots
// and indices 429..442 are reserved for music tracks; appending new entries past 447
// preserves all existing indices and stays within the native table limit.
constexpr std::size_t kMaxNativeRecords = 460;

struct Record {
    std::string name;
    std::string next;
    std::uint32_t type = 0;
    std::uint32_t mode = 0;
    std::uint32_t min_range = 0;
    std::uint32_t max_range = 0;
    std::vector<std::string> files;
};

struct PatchResult {
    bool modified = false;
    std::size_t chains_linked = 0;
    std::size_t records_added = 0;
};

bool parse(const std::vector<std::uint8_t>& raw, std::vector<Record>& out);
std::vector<std::uint8_t> serialize(const std::vector<Record>& records);

// Repairs broken multi-step dialogue chains in the shipped Android soundBase.sb
// when the corresponding audio files exist on disk (checked via asset_exists with
// paths relative to the assets root, e.g. "published/sounds/sfx/vo/tyler/Dem_16.sps").
PatchResult patch_dialogue_chains(std::vector<Record>& records,
                                  const std::function<bool(const std::string&)>& asset_exists);

}  // namespace sound_base
