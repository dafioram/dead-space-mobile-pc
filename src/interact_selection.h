#pragma once
#include <cmath>

namespace interact {

// Sight cone for distant kinesis targeting along the camera's horizontal line of sight.
constexpr float kKinesisRange = 9.0f;
constexpr float kKinesisCos = 0.97f;  // ~14 degrees
constexpr float kKinesisMaxVertical = 3.0f;

// Normal nearby interaction (E / Xbox A) excludes telekinesisable objects so it can never trigger
// GameObjectInteractive::onInteract()'s telekinesis branch.
inline bool nearby_selectable(bool hidden, bool skip_rule, bool interactive, bool interactive_plus,
                              bool telekinesis_target) {
    return !hidden && !skip_rule && !telekinesis_target && (interactive || interactive_plus);
}

// Kinesis target selection (F / Xbox B):
// 1. Prefer a telekinesis target whose zone centre falls in the sight cone (closest to crosshair).
// 2. Otherwise fall back to the best close-range telekinesis target overlapping the player's
//    interaction hitbox (candidates are already stably sorted by aim_score).
template <class Candidates>
auto best_kinesis(const Candidates& candidates) -> const typename Candidates::value_type* {
    const typename Candidates::value_type* best = nullptr;
    float best_cos = kKinesisCos;
    for (const auto& c : candidates) {
        if (!c.telekinesis_target) continue;
        const float mx = (c.local_zone.lo.x + c.local_zone.hi.x) * 0.5f;
        const float my = (c.local_zone.lo.y + c.local_zone.hi.y) * 0.5f - 0.5f;
        const float mz = (c.local_zone.lo.z + c.local_zone.hi.z) * 0.5f;
        const float len = std::sqrt(mx * mx + my * my + mz * mz);
        if (len < 0.1f || len > kKinesisRange || mz <= 0.0f) continue;
        // horizontal angle only (camera pitch is not part of the view); vertical within 3 m
        const float hl = std::sqrt(mx * mx + mz * mz);
        const float cs = mz / hl;
        if (cs > best_cos && std::fabs(my) < kKinesisMaxVertical) {
            best_cos = cs;
            best = &c;
        }
    }
    if (best) return best;
    for (const auto& c : candidates)
        if (c.telekinesis_target && c.in_hitbox) return &c;
    return nullptr;
}

}  // namespace interact
