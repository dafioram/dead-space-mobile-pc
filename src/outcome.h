#pragma once

namespace interact {

// What the game would do if E were pressed on a selectable object in reach.
//   acts:       it would do something (or at least part of it)
//   declines:   it would only refuse or ignore E (full ammo, locked door, a limb that has no use action); a
//               refusing object still gives its feedback when it is the only thing in reach
//   unverified: not verified for this object or core layout; the original behaviour is kept
enum class Outcome { unverified, acts, declines };

// Candidates already have the existing geometric order. Prefer the first selectable candidate in the hitbox
// that the game would not decline. Retain the first declining one only when no alternative exists, so its
// refusal feedback (full-ammo message, locked-door cue) still plays when it is the only thing in reach.
template <class Candidates> auto best_selectable(const Candidates& candidates) -> const typename Candidates::value_type* {
    const typename Candidates::value_type* declining = nullptr;
    for (const auto& candidate : candidates) {
        if (!candidate.selectable || !candidate.in_hitbox) continue;
        if (candidate.outcome != Outcome::declines) return &candidate;
        if (!declining) declining = &candidate;
    }
    return declining;
}

}  // namespace interact
