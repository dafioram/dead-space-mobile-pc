#pragma once

namespace interact {

// What the game would do if E were pressed on a usable object in reach.
//   available: it would act (or at least partly act)
//   blocked:   it would only refuse (full ammo, locked door); the object still gives its refusal feedback
//   unknown:   not verified for this object/layout; original behaviour is kept
enum class Eligibility { unknown, available, blocked };

// Candidates already have the existing geometric order. Prefer the first usable, reachable candidate that
// the game would not refuse. Retain the first blocked one only when no alternative exists, so its refusal
// feedback (full-ammo message, locked-door cue) still plays when it is the only thing in reach.
template <class Candidates> auto reachable(const Candidates& candidates) -> const typename Candidates::value_type* {
    const typename Candidates::value_type* blocked = nullptr;
    for (const auto& candidate : candidates) {
        if (!candidate.usable || !candidate.overlap) continue;
        if (candidate.eligibility != Eligibility::blocked) return &candidate;
        if (!blocked) blocked = &candidate;
    }
    return blocked;
}

}  // namespace interact
