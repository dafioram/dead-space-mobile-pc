#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "sound_base_patch.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    }
}

sound_base::Record make_vo(const std::string& name, const std::string& file, const std::string& next = "") {
    sound_base::Record r;
    r.name = name;
    r.next = next;
    r.type = 2;
    r.mode = 1;
    r.min_range = 5;
    r.max_range = 30;
    if (!file.empty()) r.files.push_back(file);
    return r;
}

const sound_base::Record* find(const std::vector<sound_base::Record>& recs, const std::string& name) {
    for (const auto& r : recs) {
        if (r.name == name) return &r;
    }
    return nullptr;
}

void test_round_trip_and_invalid_inputs() {
    std::vector<sound_base::Record> orig = {
        make_vo("deadspace/vandal/dem_09", "../sfx/vo/vandal/DEM_09.sps"),
        make_vo("deadspace/vandal/dem_08", "../sfx/vo/vandal/DEM_08_01.sps", "deadspace/vandal/dem_08a"),
    };
    orig.push_back({"deadspace/empty", "", 0, 1, 5, 30, {}});

    const auto bytes = sound_base::serialize(orig);
    std::vector<sound_base::Record> decoded;
    expect(sound_base::parse(bytes, decoded), "parse valid serialized soundBase");
    expect(decoded.size() == orig.size(), "round-trip record count");
    expect(sound_base::serialize(decoded) == bytes, "round-trip bytes identical");

    // Empty and truncated buffers must fail cleanly.
    std::vector<sound_base::Record> bad;
    expect(!sound_base::parse({}, bad), "reject empty buffer");
    auto truncated = bytes;
    truncated.pop_back();
    expect(!sound_base::parse(truncated, bad), "reject truncated buffer");

    // Zero-length string field must fail cleanly.
    std::vector<std::uint8_t> zero_len = {0, 0, 0, 0};
    expect(!sound_base::parse(zero_len, bad), "reject zero-length name");
}

void test_patch_repairs_all_four_category_a_chains() {
    std::vector<sound_base::Record> recs = {
        make_vo("deadspace/vandal/dem_06", "../sfx/vo/vandal/DEM_06.sps"),
        make_vo("deadspace/vandal/dem_07", "../sfx/vo/vandal/DEM_07.sps"),
        make_vo("deadspace/vandal/dem_09", "../sfx/vo/vandal/DEM_09.sps"),
        make_vo("deadspace/vandal/dem_09a", "../sfx/vo/tyler/Dem_15.sps"),
        make_vo("deadspace/sabotage/start", "../sfx/vo/sabotage/Steven_Walla_04.sps", "deadspace/sabotage/start_1"),
        make_vo("deadspace/sabotage/start_1", "../sfx/vo/sabotage/Andrew_Walla_10.sps"),
        make_vo("deadspace/sabotage/hell_is_that", "../sfx/vo/sabotage/Tom_Walla_12.sps"),
    };
    const std::set<std::string> present = {
        "published/sounds/sfx/vo/tyler/Dem_16.sps",
        "published/sounds/sfx/vo/tyler/Dem_18.sps",
    };
    auto exists = [&](const std::string& p) { return present.count(p) != 0; };

    const auto r1 = sound_base::patch_dialogue_chains(recs, exists);
    expect(r1.modified, "patch marks modified");
    expect(r1.chains_linked == 4, "4 dialogue chains linked");
    expect(r1.records_added == 2, "2 orphan VO entries appended");
    expect(recs.size() == 9, "total records grew by 2");

    // Existing record order must remain untouched.
    expect(recs[0].name == "deadspace/vandal/dem_06", "dem_06 index preserved");
    expect(recs[6].name == "deadspace/sabotage/hell_is_that", "hell_is_that index preserved");

    expect(find(recs, "deadspace/vandal/dem_09")->next == "deadspace/vandal/dem_09a", "dem_09 linked to dem_09a");
    expect(find(recs, "deadspace/sabotage/start_1")->next == "deadspace/sabotage/hell_is_that",
           "sabotage/start_1 linked to hell_is_that");

    const auto* dem06a = find(recs, "deadspace/vandal/dem_06a");
    expect(dem06a != nullptr && dem06a->type == 2 && dem06a->mode == 1 &&
               dem06a->files.size() == 1 && dem06a->files[0] == "../sfx/vo/tyler/Dem_18.sps",
           "dem_06a points to Dem_18.sps");
    expect(find(recs, "deadspace/vandal/dem_06")->next == "deadspace/vandal/dem_06a", "dem_06 linked to dem_06a");

    const auto* dem07a = find(recs, "deadspace/vandal/dem_07a");
    expect(dem07a != nullptr && dem07a->type == 2 && dem07a->mode == 1 &&
               dem07a->files.size() == 1 && dem07a->files[0] == "../sfx/vo/tyler/Dem_16.sps",
           "dem_07a points to Dem_16.sps");
    expect(find(recs, "deadspace/vandal/dem_07")->next == "deadspace/vandal/dem_07a", "dem_07 linked to dem_07a");

    // Round-trip the patched records and confirm idempotency.
    std::vector<sound_base::Record> reparsed;
    expect(sound_base::parse(sound_base::serialize(recs), reparsed), "patched soundBase re-parses cleanly");
    const auto r2 = sound_base::patch_dialogue_chains(reparsed, exists);
    expect(!r2.modified && r2.chains_linked == 0 && r2.records_added == 0, "patch is idempotent");
}

void test_missing_orphan_files_and_slot_cap() {
    std::vector<sound_base::Record> recs = {
        make_vo("deadspace/vandal/dem_06", "../sfx/vo/vandal/DEM_06.sps"),
        make_vo("deadspace/vandal/dem_07", "../sfx/vo/vandal/DEM_07.sps"),
        make_vo("deadspace/vandal/dem_09", "../sfx/vo/vandal/DEM_09.sps", "already/set"),
        make_vo("deadspace/vandal/dem_09a", "../sfx/vo/tyler/Dem_15.sps"),
    };
    // Neither orphan file exists on disk; existing next link must not be overwritten.
    const auto r1 = sound_base::patch_dialogue_chains(recs, [](const std::string&) { return false; });
    expect(!r1.modified && r1.chains_linked == 0 && r1.records_added == 0, "skips missing orphan files and existing next");
    expect(find(recs, "deadspace/vandal/dem_09")->next == "already/set", "existing next preserved");

    // At native slot limit (460), no new entries should be appended.
    while (recs.size() < sound_base::kMaxNativeRecords) {
        recs.push_back(make_vo("filler/" + std::to_string(recs.size()), "../sfx/x.sps"));
    }
    const auto r2 = sound_base::patch_dialogue_chains(recs, [](const std::string&) { return true; });
    expect(!r2.modified && recs.size() == sound_base::kMaxNativeRecords, "respects 460-slot SoundsBase cap");
}

}  // namespace

int main() {
    test_round_trip_and_invalid_inputs();
    test_patch_repairs_all_four_category_a_chains();
    test_missing_orphan_files_and_slot_cap();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("sound_base_patch_tests: ok\n");
    return 0;
}
