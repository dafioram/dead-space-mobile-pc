#pragma once

#include <algorithm>
#include <cmath>

#include "objects.h"

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

inline View interaction_view(objs::Vec3 position, objs::Vec3 facing, objs::Vec3 up, float yaw) {
    objs::Vec3 right = cross(up, facing);
    float c = std::cos(yaw), s = std::sin(yaw);
    // Match input.cpp's free-look camera orbit. Rotate the complete body frame,
    // including up; never flatten a wall-facing direction onto the world XZ plane.
    auto orbit = [c, s](objs::Vec3 v) { return objs::Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c}; };
    return {position, orbit(facing), orbit(right), orbit(up)};
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

}  // namespace interact
