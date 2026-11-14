#pragma once

#include <cmath>

#include "objects.h"

namespace camera {

// GameObject::adjustYaw multiplies the body's basis by a local-Y rotation.
// In world space that is a rotation about the body's current up, not world Y.
class YawRotation {
    objs::Vec3 axis{0, 1, 0};
    float cosine = 1, sine = 0;

public:
    YawRotation(objs::Vec3 up, float angle) {
        float length = std::hypot(up.x, up.y, up.z);
        if (!std::isfinite(length) || length < 1e-6f || !std::isfinite(angle)) return;
        axis = {up.x / length, up.y / length, up.z / length};
        cosine = std::cos(angle);
        sine = std::sin(angle);
    }

    objs::Vec3 rotate(objs::Vec3 v) const {
        float along = (axis.x * v.x + axis.y * v.y + axis.z * v.z) * (1 - cosine);
        return {v.x * cosine + (axis.y * v.z - axis.z * v.y) * sine + axis.x * along,
                v.y * cosine + (axis.z * v.x - axis.x * v.z) * sine + axis.y * along,
                v.z * cosine + (axis.x * v.y - axis.y * v.x) * sine + axis.z * along};
    }

    objs::Vec3 orbit(objs::Vec3 point, objs::Vec3 pivot) const {
        auto v = rotate({point.x - pivot.x, point.y - pivot.y, point.z - pivot.z});
        return {pivot.x + v.x, pivot.y + v.y, pivot.z + v.z};
    }
};

}  // namespace camera
