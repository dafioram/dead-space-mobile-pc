#include "telekinesis_release.h"
#include "elf_loader.h"

namespace telekinesis {
namespace {

struct Symbols {
    u32 abort_fn, get_target, beam_abort, start_fn, player_vtable, beam_vtable;
};

// Cached once every symbol is found (retried if queried before the guest library has loaded).
const Symbols& symbols() {
    static Symbols s{};
    if (!s.abort_fn || !s.get_target || !s.beam_abort || !s.start_fn || !s.player_vtable || !s.beam_vtable) {
        s = {
            elf::find("_ZN18GameObjectPlayable16abortTelekinesisEv"),
            elf::find("_ZNK18GameObjectPlayable20getTelekinesisTargetEv"),
            elf::find("_ZN25GameObjectTelekinesisBeam18onAbortTelekinesisEv"),
            elf::find("_ZN18GameObjectPlayable16startTelekinesisEP21GameObjectInteractive"),
            elf::find("_ZTV18GameObjectPlayable"),
            elf::find("_ZTV25GameObjectTelekinesisBeam"),
        };
    }
    return s;
}

template <size_t N>
bool words_match(u32 base, const Word (&words)[N]) {
    for (const Word& w : words)
        if (!mem::range_mapped(base + w.offset, 4) || mem::r32(base + w.offset) != w.word) return false;
    return true;
}

// Re-verified on every call (never cached) so an unrecognized core is not trusted.
bool supported(const Symbols& s) {
    return s.player_vtable && s.beam_vtable &&
           s.abort_fn == mem::ELF_BASE + 0x18f1c4 &&
           s.get_target == mem::ELF_BASE + 0x187540 &&
           s.beam_abort == mem::ELF_BASE + 0x19ffa8 &&
           s.start_fn == mem::ELF_BASE + 0x1930e4 &&
           words_match(s.abort_fn, kAbortWords) &&
           words_match(s.get_target, kTargetWords) &&
           words_match(s.beam_abort, kBeamAbortWords) &&
           words_match(s.start_fn, kStartWords);
}

constexpr u32 kPlayerState = 0x210;
constexpr u32 kStateTelekinesis = 0x12;
constexpr u32 kPlayerTkState = 0x5b8;
constexpr u32 kTkStateActive = 1;
constexpr u32 kPlayerBeam = 0x5d8;

constexpr u32 kBeamTarget = 0x174;
constexpr u32 kBeamState = 0x1a8;
constexpr u32 kBeamPulling = 2;
constexpr u32 kBeamHolding = 3;

constexpr u32 kObjectType = 0x28;
constexpr u32 kTypeSocketedA = 0x48;
constexpr u32 kTypeSocketedB = 0x49;
constexpr u32 kSocketedFlag = 0x234;
constexpr u32 kDropVtableBytes = 0xd8;  // covers getBody (+0xc0) and setGravity (+0xd4)

bool target_valid(u32 target) {
    if (!target || !mem::range_mapped(target, kObjectType + 4)) return false;
    const u32 vtable = mem::r32(target);
    if (vtable < 0x10000 || !mem::range_mapped(vtable, kDropVtableBytes)) return false;
    const u32 type = mem::r32(target + kObjectType);
    if ((type == kTypeSocketedA || type == kTypeSocketedB) && !mem::range_mapped(target, kSocketedFlag + 1))
        return false;
    return true;
}

}  // namespace

ReleaseStatus query_release(u32 player) {
    const Symbols& sym = symbols();
    if (!supported(sym) || !player || !mem::range_mapped(player, kPlayerBeam + 4) ||
        mem::r32(player) != sym.player_vtable + 8)
        return {};
    if (mem::r32(player + kPlayerState) != kStateTelekinesis) return {};

    const u32 tk_state = mem::r32(player + kPlayerTkState);
    const u32 beam = mem::r32(player + kPlayerBeam);
    if (!beam || !mem::range_mapped(beam, kBeamState + 4) || mem::r32(beam) != sym.beam_vtable + 8)
        return {ReleaseAction::busy, 0, 0, tk_state, 0};

    const u32 target = mem::r32(beam + kBeamTarget);
    const u32 beam_state = mem::r32(beam + kBeamState);
    if (tk_state == kTkStateActive && (beam_state == kBeamPulling || beam_state == kBeamHolding) &&
        target_valid(target))
        return {ReleaseAction::release, sym.abort_fn, target, tk_state, beam_state};

    return {ReleaseAction::busy, 0, target, tk_state, beam_state};
}

}  // namespace telekinesis
