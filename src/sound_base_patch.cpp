#include "sound_base_patch.h"

#include <cstring>

namespace sound_base {

namespace {

bool read_u32(const std::vector<std::uint8_t>& raw, std::size_t& off, std::uint32_t& out) {
    if (off + 4 > raw.size()) return false;
    std::memcpy(&out, raw.data() + off, 4);
    off += 4;
    return true;
}

bool read_cstr(const std::vector<std::uint8_t>& raw, std::size_t& off, std::string& out) {
    std::uint32_t len = 0;
    if (!read_u32(raw, off, len)) return false;
    if (len == 0 || len > raw.size() - off) return false;
    std::size_t text_len = len;
    while (text_len > 0 && raw[off + text_len - 1] == 0) --text_len;
    out.assign(reinterpret_cast<const char*>(raw.data() + off), text_len);
    off += len;
    return true;
}

void write_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    const std::size_t pos = out.size();
    out.resize(pos + 4);
    std::memcpy(out.data() + pos, &v, 4);
}

void write_cstr(std::vector<std::uint8_t>& out, const std::string& s) {
    // soundBase.sb always stores a trailing NUL byte (so empty strings have length 1).
    const std::uint32_t len = static_cast<std::uint32_t>(s.size() + 1);
    write_u32(out, len);
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

Record* find_record(std::vector<Record>& records, const char* name) {
    for (auto& r : records) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

}  // namespace

bool parse(const std::vector<std::uint8_t>& raw, std::vector<Record>& out) {
    out.clear();
    if (raw.empty()) return false;
    std::size_t off = 0;
    while (off < raw.size()) {
        Record rec;
        if (!read_cstr(raw, off, rec.name)) return false;
        if (!read_cstr(raw, off, rec.next)) return false;
        std::uint32_t file_count = 0;
        if (!read_u32(raw, off, rec.type) ||
            !read_u32(raw, off, rec.mode) ||
            !read_u32(raw, off, rec.min_range) ||
            !read_u32(raw, off, rec.max_range) ||
            !read_u32(raw, off, file_count)) {
            return false;
        }
        if (file_count > 1024) return false;
        rec.files.reserve(file_count);
        for (std::uint32_t i = 0; i < file_count; ++i) {
            std::string file;
            if (!read_cstr(raw, off, file)) return false;
            rec.files.push_back(std::move(file));
        }
        out.push_back(std::move(rec));
    }
    return !out.empty();
}

std::vector<std::uint8_t> serialize(const std::vector<Record>& records) {
    std::vector<std::uint8_t> out;
    out.reserve(records.size() * 96);
    for (const auto& rec : records) {
        write_cstr(out, rec.name);
        write_cstr(out, rec.next);
        write_u32(out, rec.type);
        write_u32(out, rec.mode);
        write_u32(out, rec.min_range);
        write_u32(out, rec.max_range);
        write_u32(out, static_cast<std::uint32_t>(rec.files.size()));
        for (const auto& file : rec.files) {
            write_cstr(out, file);
        }
    }
    return out;
}

PatchResult patch_dialogue_chains(std::vector<Record>& records,
                                  const std::function<bool(const std::string&)>& asset_exists) {
    PatchResult res;

    // 1. Existing records that are present in soundBase.sb but missing their next link:
    //    - deadspace/vandal/dem_09 -> deadspace/vandal/dem_09a (Ch. 8, Tyler Dem_15.sps)
    //    - deadspace/sabotage/start_1 -> deadspace/sabotage/hell_is_that (Ch. 1, 4-clip Walla continuation)
    struct DirectLink {
        const char* from;
        const char* to;
    };
    constexpr DirectLink kDirectLinks[] = {
        {"deadspace/vandal/dem_09", "deadspace/vandal/dem_09a"},
        {"deadspace/sabotage/start_1", "deadspace/sabotage/hell_is_that"},
    };
    for (const auto& link : kDirectLinks) {
        Record* src = find_record(records, link.from);
        Record* dst = find_record(records, link.to);
        if (src && dst && src->next.empty()) {
            src->next = link.to;
            res.modified = true;
            ++res.chains_linked;
        }
    }

    // 2. Orphan VO clips on disk that were omitted from soundBase.sb:
    //    - deadspace/vandal/dem_06 -> deadspace/vandal/dem_06a (Ch. 11, Tyler Dem_18.sps)
    //    - deadspace/vandal/dem_07 -> deadspace/vandal/dem_07a (Ch. 11, Tyler Dem_16.sps)
    struct OrphanLink {
        const char* from;
        const char* new_key;
        const char* sps_rel_in_sb;
        const char* asset_rel;
    };
    constexpr OrphanLink kOrphanLinks[] = {
        {"deadspace/vandal/dem_06", "deadspace/vandal/dem_06a",
         "../sfx/vo/tyler/Dem_18.sps", "published/sounds/sfx/vo/tyler/Dem_18.sps"},
        {"deadspace/vandal/dem_07", "deadspace/vandal/dem_07a",
         "../sfx/vo/tyler/Dem_16.sps", "published/sounds/sfx/vo/tyler/Dem_16.sps"},
    };
    for (const auto& orphan : kOrphanLinks) {
        Record* src = find_record(records, orphan.from);
        if (!src || !src->next.empty()) continue;

        if (!find_record(records, orphan.new_key)) {
            if (records.size() >= kMaxNativeRecords) continue;
            if (!asset_exists || !asset_exists(orphan.asset_rel)) continue;
            Record added;
            added.name = orphan.new_key;
            added.type = 2;       // dedicated VO/dialogue channel
            added.mode = 1;       // standard VO mode (matches all other dialogue entries)
            added.min_range = 5;
            added.max_range = 30;
            added.files.push_back(orphan.sps_rel_in_sb);
            records.push_back(std::move(added));
            // Re-lookup src in case push_back reallocated records.
            src = find_record(records, orphan.from);
            ++res.records_added;
        }
        if (src) {
            src->next = orphan.new_key;
            res.modified = true;
            ++res.chains_linked;
        }
    }

    return res;
}

}  // namespace sound_base
