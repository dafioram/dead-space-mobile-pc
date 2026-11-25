// Optional startup helper (--unlock-hard) that unlocks Hard difficulty through the
// game's own Settings::unlockHard() / Settings::clearNotifiedUnlockedHard() methods
// right after Settings::load() finishes.
#pragma once

namespace unlock_hard {

void set_requested(bool enabled);
bool requested();

// Installs the post-Settings::load hook when --unlock-hard was requested.
// Returns true immediately (without touching guest code) when not requested.
bool install();

}  // namespace unlock_hard
