#pragma once
#include "eligibility.h"
#include "memory.h"

namespace door {
// Read-only: would GameObjectDoor::onOpen (what E runs on a door) do anything? Game thread only.
//   blocked:   it would refuse or ignore the press. The game's own code decides:
//                - state (+0x1e4) is not 1 or 2: onOpen returns at once, nothing happens;
//                - locked: the byte at +0x1ec is set, or the int at +0x170 is above 0, or the byte at +0x3c0 is
//                  set: it plays the "locked" feedback and does not open.
//              Script locks and power-node locks both set +0x1ec (GameObjectDoor::onLock / onUnlock).
//   available: verified door that is closed and unlocked: onOpen would start opening it.
//   unknown:   not exactly a GameObjectDoor, or the core's code is not the verified one.
interact::Eligibility open_eligibility(u32 object);

// Instruction words of onOpen (offset from the function start) that the layout assumptions rest on:
// the state test, the three lock tests and the branches that act on them. All are checked on every query.
struct Word {
    u32 offset, word;
};
constexpr Word kOnOpenWords[] = {
    {0x04, 0xe59031e4},   // ldr   r3, [r0, #0x1e4]   state
    {0x10, 0xe2433001},   // sub   r3, r3, #1
    {0x14, 0xe3530001},   // cmp   r3, #1
    {0x18, 0x9a000001},   // bls   -> lock tests (state 1 or 2), else return
    {0x24, 0xe5d031ec},   // ldrb  r3, [r0, #0x1ec]   lock flag
    {0x28, 0xe3530000},   // cmp   r3, #0
    {0x2c, 0x1a000002},   // bne   -> locked feedback
    {0x30, 0xe5903170},   // ldr   r3, [r0, #0x170]
    {0x34, 0xe3530000},   // cmp   r3, #0
    {0x38, 0xda000035},   // ble   -> third test
    {0x114, 0xe5d053c0},  // ldrb  r5, [r0, #0x3c0]
    {0x118, 0xe1a05c05},  // lsl   r5, r5, #0x18
    {0x11c, 0xe1b05c45},  // asrs  r5, r5, #0x18
    {0x120, 0x1affffc5},  // bne   -> locked feedback
};
}  // namespace door
