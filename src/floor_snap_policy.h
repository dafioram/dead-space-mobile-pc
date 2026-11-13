// Bounded use of the game's wider floor probe when grounded locomotion stalls.
#pragma once

#include <algorithm>
#include <cmath>

#include "objects.h"

namespace movement {

class FloorSnapRecovery {
public:
    void reset() { *this = {}; }

    void update(bool eligible, float move_amount, objs::Vec3 position, int dt_ms) {
        if (!eligible || !std::isfinite(move_amount) || move_amount <= 0 || dt_ms <= 0 || !std::isfinite(position.x) ||
            !std::isfinite(position.y) || !std::isfinite(position.z)) {
            reset();
            return;
        }
        int dt = std::min(dt_ms, 100);
        objs::Vec3 delta{position.x - previous_.x, position.y - previous_.y, position.z - previous_.z};
        bool had_previous = sampled_;
        previous_ = position;
        sampled_ = true;
        if (sweep_ms_ > 0) {
            sweep_ms_ = std::max(0, sweep_ms_ - dt);
            if (sweep_ms_ == 0) cooldown_ms_ = 200;
            return;
        }
        if (cooldown_ms_ > 0) {
            cooldown_ms_ = std::max(0, cooldown_ms_ - dt);
            return;
        }
        if (!had_previous) return;
        // Ordinary walking is much faster than this. Do not treat slow rendering
        // as a stall: compare displacement with elapsed simulation time.
        float minimum_step = 0.25f * std::min(move_amount, 1.0f) * float(dt) / 1000.0f;
        float distance_sq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        blocked_ms_ = distance_sq < minimum_step * minimum_step ? blocked_ms_ + dt : 0;
        if (blocked_ms_ >= 100) {
            sweep_ms_ = 400;
            blocked_ms_ = 0;
        }
    }

    bool active() const { return sweep_ms_ > 0; }

private:
    objs::Vec3 previous_{};
    bool sampled_ = false;
    int blocked_ms_ = 0, sweep_ms_ = 0, cooldown_ms_ = 0;
};

}  // namespace movement
