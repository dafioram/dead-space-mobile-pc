#pragma once

#include <algorithm>
#include <cmath>

#include "objects.h"
#include "camera_geometry.h"

namespace interact {

inline float dot(objs::Vec3 a, objs::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline objs::Vec3 cross(objs::Vec3 a, objs::Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// The interaction frame follows the surface the player is standing on, not world Y.
// Use the same basis for selection and for drawing the debug volume.
struct View {
    objs::Vec3 p, f, r, u;
};

inline View world_view() { return {{}, {0, 0, 1}, {1, 0, 0}, {0, 1, 0}}; }

inline View interaction_view(objs::Vec3 position, objs::Vec3 facing, objs::Vec3 up, float yaw) {
    objs::Vec3 right = cross(up, facing);
    // Selection and camera orbit must turn about the same surface-relative up,
    // including on walls, ceilings and interpolated surface transitions.
    camera::YawRotation rotation(up, yaw);
    return {position, rotation.rotate(facing), rotation.rotate(right), rotation.rotate(up)};
}

inline objs::Vec3 view_local(const View& v, objs::Vec3 w) {
    objs::Vec3 d{w.x - v.p.x, w.y - v.p.y, w.z - v.p.z};
    return {dot(d, v.r), dot(d, v.u), dot(d, v.f)};
}

inline objs::Vec3 view_world(const View& v, objs::Vec3 p) {
    return {v.p.x + v.r.x * p.x + v.u.x * p.y + v.f.x * p.z,
            v.p.y + v.r.y * p.x + v.u.y * p.y + v.f.y * p.z,
            v.p.z + v.r.z * p.x + v.u.z * p.y + v.f.z * p.z};
}

struct Zone {
    View frame = world_view();
    objs::Box box;  // metres in frame; world-aligned bounds keep the identity frame
};

inline View region_frame(const objs::Region& region) {
    View v{region.p, region.forward, region.right, region.up};
    // Node transforms can contain model scale. Interaction dimensions are metres,
    // so retain only orthogonal unit axes, including reflected model frames.
    for (auto* axis : {&v.f, &v.r, &v.u}) {
        float length = std::hypot(axis->x, axis->y, axis->z);
        if (!std::isfinite(length) || length < 1e-6f) {
            v = world_view();
            v.p = region.p;
            return v;
        }
        *axis = {axis->x / length, axis->y / length, axis->z / length};
    }
    if (std::fabs(dot(v.r, v.u)) > 1e-3f || std::fabs(dot(v.r, v.f)) > 1e-3f ||
        std::fabs(dot(v.u, v.f)) > 1e-3f) {
        v = world_view();
        v.p = region.p;
    }
    return v;
}

inline void zone_corners(const Zone& zone, objs::Vec3 out[8]) {
    for (int i = 0; i < 8; ++i) {
        const auto& b = zone.box;
        out[i] = view_world(zone.frame, {i & 1 ? b.hi.x : b.lo.x, i & 2 ? b.hi.y : b.lo.y, i & 4 ? b.hi.z : b.lo.z});
    }
}

inline objs::Box zone_bounds(const Zone& zone, const View& view) {
    objs::Vec3 corners[8];
    zone_corners(zone, corners);
    objs::Box b{{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
    for (auto point : corners) {
        auto p = view_local(view, point);
        b.lo = {std::min(b.lo.x, p.x), std::min(b.lo.y, p.y), std::min(b.lo.z, p.z)};
        b.hi = {std::max(b.hi.x, p.x), std::max(b.hi.y, p.y), std::max(b.hi.z, p.z)};
    }
    return b;
}

// Separating-axis test for two oriented boxes. An enclosing view-space AABB is
// useful for scoring, but accepting it would select empty corners outside the
// rotated box shown by the debug overlay. Test both boxes' axes and all 9 crosses.
inline bool zones_overlap(const Zone& a, const Zone& b) {
    auto centre = [](const objs::Box& box) {
        return objs::Vec3{(box.lo.x + box.hi.x) * 0.5f, (box.lo.y + box.hi.y) * 0.5f,
                          (box.lo.z + box.hi.z) * 0.5f};
    };
    auto half = [](const objs::Box& box) {
        return objs::Vec3{(box.hi.x - box.lo.x) * 0.5f, (box.hi.y - box.lo.y) * 0.5f,
                          (box.hi.z - box.lo.z) * 0.5f};
    };
    auto ac = view_world(a.frame, centre(a.box)), bc = view_world(b.frame, centre(b.box));
    objs::Vec3 d{bc.x - ac.x, bc.y - ac.y, bc.z - ac.z};
    auto ah = half(a.box), bh = half(b.box);
    auto separated = [&](objs::Vec3 axis) {
        if (dot(axis, axis) < 1e-10f) return false;  // parallel edges do not supply an axis
        float ar = std::fabs(dot(axis, a.frame.r)) * ah.x + std::fabs(dot(axis, a.frame.u)) * ah.y +
                   std::fabs(dot(axis, a.frame.f)) * ah.z;
        float br = std::fabs(dot(axis, b.frame.r)) * bh.x + std::fabs(dot(axis, b.frame.u)) * bh.y +
                   std::fabs(dot(axis, b.frame.f)) * bh.z;
        return std::fabs(dot(d, axis)) >= ar + br;
    };
    for (auto axis : {a.frame.r, a.frame.u, a.frame.f, b.frame.r, b.frame.u, b.frame.f})
        if (separated(axis)) return false;
    for (auto aa : {a.frame.r, a.frame.u, a.frame.f})
        for (auto ba : {b.frame.r, b.frame.u, b.frame.f})
            if (separated(cross(aa, ba))) return false;
    return true;
}

// Large objects can extend into reach even when their origin is far away.
// Measure from the closest point of the interaction zone, not the object's pivot.
inline bool zone_within_radius(const objs::Box& zone, objs::Vec3 point, float radius) {
    for (float value : {zone.lo.x, zone.lo.y, zone.lo.z, zone.hi.x, zone.hi.y, zone.hi.z,
                        point.x, point.y, point.z, radius}) {
        if (!std::isfinite(value)) return false;
    }
    if (radius < 0 || zone.lo.x > zone.hi.x || zone.lo.y > zone.hi.y || zone.lo.z > zone.hi.z)
        return false;
    float dx = std::max({zone.lo.x - point.x, point.x - zone.hi.x, 0.0f});
    float dy = std::max({zone.lo.y - point.y, point.y - zone.hi.y, 0.0f});
    float dz = std::max({zone.lo.z - point.z, point.z - zone.hi.z, 0.0f});
    return std::hypot(dx, dy, dz) <= radius;
}

inline bool zone_within_radius(const Zone& zone, objs::Vec3 point, float radius) {
    return zone_within_radius(zone.box, view_local(zone.frame, point), radius);
}

}  // namespace interact
