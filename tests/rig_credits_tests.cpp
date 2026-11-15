// Synthetic SBIN schemas/layouts only. No game assets, saves or extracted bytes.
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "rig_credits.h"
#include "sbin.h"

namespace {
using Bytes = std::vector<uint8_t>;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class T> void append(Bytes& bytes, T value) {
    const auto* p = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), p, p + sizeof value);
}
template <class T> void put(Bytes& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}
template <class T> T get(const uint8_t* p) {
    T value;
    std::memcpy(&value, p, sizeof value);
    return value;
}
void chunk(Bytes& file, const char* tag, const Bytes& body) {
    file.insert(file.end(), tag, tag + 4);
    append(file, uint32_t(body.size()));
    append(file, uint32_t(0));  // the parser does not require source chunk hashes
    file.insert(file.end(), body.begin(), body.end());
    while (file.size() % 4) file.push_back(0);
}

sbin::Database schema(bool missing_align = false) {
    std::vector<std::string> strings;
    auto intern = [&](const std::string& s) {
        strings.push_back(s);
        return uint16_t(strings.size());
    };
    struct Field { const char* name; uint16_t type, offset; };
    Bytes structures, fields;
    uint16_t field_count = 0;
    auto structure = [&](const char* name, std::initializer_list<Field> members) {
        append(structures, intern(name));
        append(structures, field_count);
        append(structures, uint16_t(members.size()));
        for (const auto& f : members) {
            append(fields, intern(f.name));
            append(fields, f.type);
            append(fields, f.offset);
            append(fields, uint16_t(0));
            ++field_count;
        }
    };
    structure("Layout", {{"layoutId", 13, 0}, {"entities", 17, 4}});
    structure("Entity", {{"entityName", 13, 0}, {"type", 13, 2}, {"image", 13, 4},
                         {missing_align ? "unknownAlign" : "align", 13, 6}, {"font", 13, 8},
                         {"colorId", 13, 10}, {"width", 10, 12}, {"height", 10, 16},
                         {"vertices", 17, 20}, {"isVisible", 9, 24}});
    structure("Vertex", {{"x", 10, 0}, {"y", 10, 4}, {"anchorx", 10, 8}, {"anchory", 10, 12}});
    Bytes strs;
    append(strs, uint32_t(strings.size()));
    for (const auto& s : strings) {
        append(strs, uint32_t(s.size()));
        strs.insert(strs.end(), s.begin(), s.end());
    }
    Bytes headers, data;
    append(headers, uint32_t(2));  // an empty synthetic root array
    append(data, uint16_t(5));
    append(data, uint16_t(0));
    append(data, uint32_t(0));
    Bytes file{'S', 'B', 'I', 'N'};
    append(file, uint32_t(2));
    chunk(file, "STRU", structures);
    chunk(file, "FIEL", fields);
    chunk(file, "STRS", strs);
    chunk(file, "OHDR", headers);
    chunk(file, "DATA", data);
    sbin::Database db;
    require(db.parse(file), "synthetic schema must parse");
    return db;
}

struct Fixture {
    sbin::Database db;
    size_t entities = 0;
    Bytes original;
    Fixture(bool missing_align = false, bool missing_icon = false, bool bad_vertex = false)
        : db(schema(missing_align)) {
        auto entry = [&](const char* name, const char* type, const char* image, float x, float width) {
            Bytes points(16);
            put(points, 0, x);
            put(points, 4, 7.0f);
            put(points, 8, 0.25f);
            put(points, 12, 0.75f);
            size_t array = db.add_array(16, 2, 1, points);
            Bytes e(size_t(db.struct_size(1)));
            put(e, 0, db.add_string(name));
            put(e, 2, db.add_string(type));
            put(e, 4, db.add_string(image));
            put(e, 6, db.add_string("HCENTER|TOP"));
            put(e, 8, db.add_string("SYNTHETIC_FONT"));
            put(e, 10, db.add_string("SYNTHETIC_TINT"));
            put(e, 12, width);
            put(e, 16, 31.0f);
            put(e, 20, bad_vertex && std::string(name) == "NODE_AMMOUNT" ? int32_t(-1) : int32_t(array));
            e[24] = 1;
            original.insert(original.end(), e.begin(), e.end());
        };
        entry("NODE_BACKGROUND", "IMAGE", "rig/rig_node_bg.tif", 3.0f, 81.0f);
        if (!missing_icon) entry("ICON NODE", "IMAGE", "synthetic/node.tif", 41.0f, 29.0f);
        entry("NODE_AMMOUNT", "MUTABLETEXT", "", 25.0f, 0.0f);
        entities = db.add_array(16, 1, uint32_t(original.size() / size_t(db.struct_size(1))), original);
        Bytes layout(8);
        put(layout, 0, db.add_string("HUD_RIG_NODES"));
        put(layout, 4, int32_t(entities));
        db.add_struct_object(0, layout);
    }
    const uint8_t* entry(size_t i) {
        return db.body(entities) + i * db.array_stride(entities);
    }
    std::string string_at(size_t i, size_t offset) { return db.string(get<uint16_t>(entry(i) + offset)); }
    size_t points(size_t i) { return size_t(get<int32_t>(entry(i) + 20)); }
    const uint8_t* point(size_t i, size_t p = 0) {
        return db.body(points(i)) + p * db.array_stride(points(i));
    }
};

void test_counter() {
    Fixture f;
    require(rig_credits::patch_layout(f.db), "supported layout must be patched");
    require(f.db.array_count(f.entities) == 6, "append exactly three noninteractive entities");
    require(std::memcmp(f.db.body(f.entities), f.original.data(), f.original.size()) == 0,
            "keep all original node entities/vertex references unchanged");
    require(f.string_at(3, 0) == "PORT_CREDIT_BACKGROUND", "unique background name");
    require(f.string_at(4, 0) == "PORT_CREDIT_ICON", "unique icon name");
    require(f.string_at(5, 0) == "PORT_CREDIT_AMOUNT", "unique text name");
    require(f.string_at(3, 2) == "STRETCH" && f.string_at(4, 2) == "STRETCH" && f.string_at(5, 2) == "MUTABLETEXT",
            "no button or actionable inventory item is created");
    require(f.string_at(3, 4) == "rig/rig_node_bg.tif", "reuse node background artwork");
    require(f.string_at(4, 4) == "items/item_credit.tif", "use existing credits artwork");
    require(f.string_at(5, 8) == "SYNTHETIC_FONT" && f.string_at(5, 10) == "SYNTHETIC_TINT",
            "inherit template font and colour rather than hardcoding a font");
    require(f.string_at(5, 6) == "RIGHT|TOP", "align the amount beside its currency icon");
    require(f.db.array_count(f.points(3)) == 2 && f.db.array_count(f.points(4)) == 2,
            "native STRETCH images need two explicit draw-bound vertices");
    require(get<float>(f.point(4, 1)) - get<float>(f.point(4)) == 36.0f &&
            get<float>(f.point(4, 1) + 4) - get<float>(f.point(4) + 4) == 14.0f,
            "currency image is bounded to a small wide icon, not its intrinsic texture size");
    require(get<float>(f.entry(4) + 12) == 0 && get<float>(f.entry(4) + 16) == 0,
            "do not rely on ignored IMAGE width/height metadata for draw sizing");
    require(get<float>(f.point(3, 1)) < get<float>(f.point(0)),
            "credits tile stays left of the node tile with a gap");
    require(get<float>(f.point(4)) > get<float>(f.point(3)) &&
            get<float>(f.point(4, 1)) < get<float>(f.point(5)) - 80.0f,
            "icon stays left of the full ten-digit amount and inside its tile");
    require(get<float>(f.point(5)) == 25.0f - 160.0f + 150.0f - 81.0f - 8.0f,
            "keep the user-confirmed amount position unchanged");
    require(get<float>(f.point(4) + 4) > get<float>(f.point(3) + 4) &&
            get<float>(f.point(4, 1) + 4) < get<float>(f.point(3, 1) + 4),
            "smaller currency icon is vertically inset in its panel");
    for (size_t i = 3; i < 6; ++i) {
        if (i != 4) require(get<float>(f.point(i) + 4) == 7.0f, "preserve panel/text baseline");
        require(get<float>(f.point(i) + 8) == 0.25f && get<float>(f.point(i) + 12) == 0.75f,
                "preserve native anchors");
        if (i != 5)
            require(get<float>(f.point(i, 1) + 8) == 0.25f && get<float>(f.point(i, 1) + 12) == 0.75f,
                    "both corners keep identical native anchors");
        require(f.entry(i)[24] == 1, "preserve template visibility");
    }
    auto once = f.db.serialize();
    require(rig_credits::patch_layout(f.db), "repeated patch must remain supported");
    require(f.db.serialize() == once, "repeated patch must not append more counters");
    sbin::Database round_trip;
    require(round_trip.parse(once), "patched data must round-trip");
    require(round_trip.array_count(f.entities) == 6, "round-trip preserves added entities");
}

void test_fail_closed() {
    for (int fault = 0; fault < 3; ++fault) {
        Fixture f(fault == 0, fault == 1, fault == 2);
        const auto before = f.db.serialize();
        require(!rig_credits::patch_layout(f.db), "unsupported schema/template must not be patched");
        require(f.db.serialize() == before, "failure must leave the input unchanged");
    }
    auto missing_layout = schema();
    const auto before = missing_layout.serialize();
    require(!rig_credits::patch_layout(missing_layout), "missing target layout must be rejected");
    require(missing_layout.serialize() == before, "missing layout must stay unchanged");
}

}  // namespace

int main() {
    try {
        test_counter();
        test_fail_closed();
        std::cout << "RIG credits synthetic layout tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
