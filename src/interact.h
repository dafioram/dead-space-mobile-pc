// "Use" and "Kinesis" buttons (game thread only).
//
// Nearby interact (E / A): a hitbox in front of the player (pointing where the camera looks,
// flattened to horizontal) is tested against each non-telekinesis usable object's interaction zone
// (its bounding box, padded to a minimum size, adjusted per class). The overlapping object nearest
// the line of sight is used; nothing else.
//
// Kinesis (F / B): releases the currently held object if carrying one; otherwise grabs the best
// telekinesis target along the line of sight (or overlapping the nearby hitbox as fallback).
#pragma once
#include <string>

#include "memory.h"
#include "objects.h"

namespace interact {

// look_yaw: extra rotation of the view relative to the player's facing (free look orbit).
// When combined_kinesis is false (Modern preset): nearby non-telekinesis interaction only.
// When combined_kinesis is true (Classic Port preset): nearby objects plus 9 m kinesis fallback
// (and non-throw release if carrying an object).
// Returns true if something was used. report: log every candidate (F9 / dev "interact").
bool use(u32 player, u32 world, float look_yaw, bool report, bool combined_kinesis = false);
// Kinesis grab / non-throw release only; never operates nearby non-telekinesis objects.
bool kinesis(u32 player, u32 world, float look_yaw, bool report);
// Same selection without acting; returns a description of what E (and F in Modern) would pick.
std::string preview(u32 player, u32 world, float look_yaw, bool combined_kinesis = false);

// Save repair (call about twice a second, game thread): a control panel saved as already used
// ("switched") while the current objective still asks for it can never be used again; after the
// objective has pointed at such a panel for a few seconds, switch the panel back on.
void repair_objective_panel(u32 world);

// Debug view (F10): draws the hitbox (cyan), usable zones nearby (white) and the one E would
// use (green) over the game (plus F's kinesis pick in amber in Modern). Call once per game frame.
void draw_debug(u32 player, u32 world, float look_yaw, bool combined_kinesis = false);

// Debug / testing
struct Approach {
    objs::Vec3 stand, target;  // where to stand (player position), what to face
};
// Where a player would stand to use obj, `dist` m out from its interaction zone on the side the
// object faces (its "use" side).
Approach approach_point(u32 obj, float dist);
// "Class addr" of what E would use right now, or "" for nothing (no side effects).
std::string pick_name(u32 player, u32 world, float look_yaw, bool combined_kinesis = false);

}  // namespace interact
