#pragma once
#include "memory.h"

namespace telekinesis {

enum class ReleaseAction {
    none,     // not in telekinesis state (or core unverified): run normal interact::use()
    release,  // actively pulling or holding a valid target: call GameObjectPlayable::abortTelekinesis()
    busy,     // in telekinesis transition (starting, beam extending, throwing or already aborting): ignore E
};

struct ReleaseStatus {
    ReleaseAction action = ReleaseAction::none;
    u32 abort_fn = 0;
    u32 target = 0;
    u32 tk_state = 0;
    u32 beam_state = 0;
};

// Instruction words of the verified guest routines that the layout and state assumptions rest on.
// Re-checked on every query so an unrecognized guest core is never trusted.
struct Word {
    u32 offset, word;
};
constexpr Word kAbortWords[] = {
    {0x00, 0xe59025b8},  // ldr  r2, [r0, #0x5b8]   player telekinesis state
    {0x04, 0xe2422002},  // sub  r2, r2, #2
    {0x08, 0xe3520001},  // cmp  r2, #1
    {0x0c, 0x912fff1e},  // bxls lr                 already firing (2) or aborting (3)
    {0x14, 0xe3a01003},  // mov  r1, #3             TelekinesisState::abort
    {0x18, 0xe3822004},  // orr  r2, r2, #4         request animation refresh
};
constexpr Word kTargetWords[] = {
    {0x00, 0xe59035d8},  // ldr  r3, [r0, #0x5d8]   player telekinesis beam pointer
    {0x04, 0xe5930174},  // ldr  r0, [r3, #0x174]   beam target pointer
};
constexpr Word kBeamAbortWords[] = {
    {0x04, 0xe59031a8},  // ldr  r3, [r0, #0x1a8]   beam state
    {0x0c, 0xe3530001},  // cmp  r3, #1
    {0x18, 0xe3530003},  // cmp  r3, #3
    {0x1c, 0xda000003},  // ble  -> onDropObject (beam states 2 and 3)
};
constexpr Word kStartWords[] = {
    {0x04, 0xe5905210},  // ldr  r5, [r0, #0x210]   player state
    {0x58, 0xe3a01012},  // mov  r1, #0x12          State::telekinesis
};

// Read-only live query on the game thread. Reads the player's current state, telekinesis sub-state,
// beam pointer, beam state and target pointer without mutating guest state.
ReleaseStatus query_release(u32 player);

}  // namespace telekinesis
