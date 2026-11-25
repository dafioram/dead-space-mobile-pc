// Unlocks all 6 native SFX slots in im::SoundManager (preventing PreloadMusic from
// throttling them to 4 via im::channelMod = 2) and adds on-demand finished-voice reclaim
// plus oldest-voice stealing so new sounds are not silently dropped during combat.
#pragma once

namespace audio_voices {

// Validates guest binary signatures before patching or hooking.
// Returns false and leaves guest memory untouched for unrecognized builds.
bool install();

}  // namespace audio_voices
