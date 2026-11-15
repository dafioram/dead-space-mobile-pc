#include "rig_credits.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "sbin.h"

namespace rig_credits {
namespace {

template <class T> T read(const uint8_t* p) {
    T value;
    std::memcpy(&value, p, sizeof value);
    return value;
}
template <class T> void write(uint8_t* p, T value) { std::memcpy(p, &value, sizeof value); }

}  // namespace

bool patch_layout(sbin::Database& db) {
    const int layout = db.find_struct("Layout"), entity = db.find_struct("Entity"), vertex = db.find_struct("Vertex");
    if (layout < 0 || entity < 0 || vertex < 0) return false;
    const int id = db.field_offset(layout, "layoutId"), entities = db.field_offset(layout, "entities");
    const int name = db.field_offset(entity, "entityName"), type = db.field_offset(entity, "type"),
              image = db.field_offset(entity, "image"), vertices = db.field_offset(entity, "vertices"),
              width = db.field_offset(entity, "width"), height = db.field_offset(entity, "height"),
              align = db.field_offset(entity, "align");
    const int x = db.field_offset(vertex, "x"), y = db.field_offset(vertex, "y");
    if (id < 0 || entities < 0 || name < 0 || type < 0 || image < 0 || vertices < 0 ||
        width < 0 || height < 0 || align < 0 || x < 0 || y < 0) return false;
    const uint16_t target = db.find_string("HUD_RIG_NODES");
    if (!target) return false;
    int32_t array = -1;
    for (size_t i = 0; i < db.object_count(); ++i) {
        if (db.kind(i) == 0 && db.struct_of(i) == layout && read<uint16_t>(db.body(i) + id) == target) {
            array = read<int32_t>(db.body(i) + entities);
            break;
        }
    }
    if (array < 0 || size_t(array) >= db.object_count() || db.kind(size_t(array)) != 2 ||
        db.array_elem_type(size_t(array)) != 16 || db.array_struct(size_t(array)) != entity) return false;
    const uint32_t stride = db.array_stride(size_t(array)), count = db.array_count(size_t(array));
    if (stride != uint32_t(db.struct_size(entity)) || !count) return false;
    std::vector<uint8_t> original(db.body(size_t(array)), db.body(size_t(array)) + size_t(count) * stride);
    std::vector<uint8_t> background, icon, amount;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = original.data() + size_t(i) * stride;
        const auto& entity_name = db.string(read<uint16_t>(e + name));
        if (entity_name == "PORT_CREDIT_AMOUNT") return true;  // already patched
        const auto& entity_type = db.string(read<uint16_t>(e + type));
        if (entity_type == "IMAGE" && db.string(read<uint16_t>(e + image)) == "rig/rig_node_bg.tif")
            background.assign(e, e + stride);
        if (entity_type == "IMAGE" && entity_name == "ICON NODE") icon.assign(e, e + stride);
        if (entity_type == "MUTABLETEXT" && entity_name == "NODE_AMMOUNT") amount.assign(e, e + stride);
    }
    if (background.empty() || icon.empty() || amount.empty()) return false;

    // Validate/copy all templates before appending anything: additions may move DATA.
    struct Copy {
        std::vector<uint8_t> entity, points;
        uint16_t element_type, structure;
        uint32_t count, stride;
    };
    std::vector<Copy> copies;
    for (const auto* source : {&background, &icon, &amount}) {
        const int32_t a = read<int32_t>(source->data() + vertices);
        if (a < 0 || size_t(a) >= db.object_count() || db.kind(size_t(a)) != 2 ||
            db.array_elem_type(size_t(a)) != 16 || db.array_struct(size_t(a)) != vertex || !db.array_count(size_t(a)))
            return false;
        const uint32_t n = db.array_count(size_t(a)), step = db.array_stride(size_t(a));
        if (step != uint32_t(db.struct_size(vertex))) return false;
        copies.push_back({*source, {db.body(size_t(a)), db.body(size_t(a)) + size_t(n) * step},
                          db.array_elem_type(size_t(a)), db.array_struct(size_t(a)), n, step});
    }

    // Keep the accepted amount position. As on the nodes counter, the icon sits
    // immediately right of the number; explicit bounds keep it clear of nodes.
    constexpr float panel_shift = -160.0f, panel_width = 150.0f;
    constexpr float icon_height = 28.0f, amount_width = 80.0f, gap = 8.0f, icon_gap = 6.0f;
    const char* names[] = {"PORT_CREDIT_BACKGROUND", "PORT_CREDIT_ICON", "PORT_CREDIT_AMOUNT"};
    const float node_width = read<float>(background.data() + width);
    const float amount_dx = panel_shift + panel_width - node_width - gap;
    const float amount_right = read<float>(copies[2].points.data() + x) + amount_dx;
    const float node_left = read<float>(copies[0].points.data() + x);
    const float icon_left = amount_right + icon_gap;
    const float icon_width = std::min(56.0f, node_left - 2.0f - icon_left);
    const float panel_left = amount_right - amount_width - gap;
    const float panel_right = icon_left + icon_width;
    const float panel_top = read<float>(copies[0].points.data() + y);
    const float panel_height = read<float>(background.data() + height);
    if (!std::isfinite(amount_right) || !std::isfinite(node_left) || !std::isfinite(panel_top) ||
        !std::isfinite(panel_height) || icon_width <= 0 || panel_height < icon_height) return false;
    for (size_t i = 0; i < copies.size(); ++i) {
        auto& copy = copies[i];
        auto* e = copy.entity.data();
        write<uint16_t>(e + name, db.add_string(names[i]));
        if (i == 2) {
            write<uint16_t>(e + align, db.add_string("RIGHT|TOP"));
            for (uint32_t p = 0; p < copy.count; ++p) {
                auto* v = copy.points.data() + size_t(p) * copy.stride;
                write<float>(v + x, read<float>(v + x) + amount_dx);
            }
        } else {
            // IMAGE calls drawImage(image, x, y): its XML width/height are not
            // draw bounds. STRETCH calls the sized overload, using two vertices.
            // Otherwise the large store/pickup credits texture covers the nodes.
            write<uint16_t>(e + type, db.add_string("STRETCH"));
            write<float>(e + width, 0.0f);
            write<float>(e + height, 0.0f);
            if (i == 1) write<uint16_t>(e + image, db.add_string("items/item_credit.tif"));
            const std::vector<uint8_t> point(copy.points.begin(), copy.points.begin() + copy.stride);
            copy.points = point;
            copy.points.insert(copy.points.end(), point.begin(), point.end());
            copy.count = 2;
            const float left = i == 0 ? panel_left : icon_left;
            const float top = i == 0 ? panel_top : panel_top + (panel_height - icon_height) * 0.5f;
            const float w = i == 0 ? panel_right - panel_left : icon_width;
            const float h = i == 0 ? panel_height : icon_height;
            write<float>(copy.points.data() + x, left);
            write<float>(copy.points.data() + y, top);
            write<float>(copy.points.data() + copy.stride + x, left + w);
            write<float>(copy.points.data() + copy.stride + y, top + h);
        }
        const auto added = db.add_array(copy.element_type, copy.structure, copy.count, copy.points);
        write<int32_t>(e + vertices, int32_t(added));
        original.insert(original.end(), copy.entity.begin(), copy.entity.end());
    }
    db.add_array(16, uint16_t(entity), count + uint32_t(copies.size()), original);
    db.replace_with_last(size_t(array));
    return true;
}

}  // namespace rig_credits
