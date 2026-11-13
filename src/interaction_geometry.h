#pragma once

#include <algorithm>
#include <cmath>

#include "objects.h"

namespace interact {

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
