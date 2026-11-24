#include "telekinesis_release.h"
#include "elf_loader.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {

using A = telekinesis::ReleaseAction;
void require(bool okay, const char* why) {
    if (!okay) throw std::runtime_error(why);
}

std::unordered_map<std::string, u32> symbols;

constexpr u32 kPlayerTable = mem::ELF_BASE + 0x1100;
constexpr u32 kBeamTable = mem::ELF_BASE + 0x1200;
constexpr u32 kTargetTable = mem::ELF_BASE + 0x1300;

constexpr u32 kAbortFn = mem::ELF_BASE + 0x18f1c4;
constexpr u32 kTargetFn = mem::ELF_BASE + 0x187540;
constexpr u32 kBeamAbortFn = mem::ELF_BASE + 0x19ffa8;
constexpr u32 kStartFn = mem::ELF_BASE + 0x1930e4;

template <size_t N>
void write_words(u32 base, const telekinesis::Word (&words)[N]) {
    for (const auto& w : words) mem::w32(base + w.offset, w.word);
}

void write_signatures() {
    write_words(kAbortFn, telekinesis::kAbortWords);
    write_words(kTargetFn, telekinesis::kTargetWords);
    write_words(kBeamAbortFn, telekinesis::kBeamAbortWords);
    write_words(kStartFn, telekinesis::kStartWords);
}

void run_tests() {
    require(mem::init(), "reserve synthetic memory; no guest/JIT execution");
    mem::commit(mem::ELF_BASE, 0x1b0000);
    // Signature-only synthetic words, not copied game routines or executable fixtures.
    write_signatures();

    const u32 player = mem::heap_alloc(0x600);
    const u32 beam = mem::heap_alloc(0x200);
    const u32 target = mem::heap_alloc(0x280);

    auto reset_objects = [&] {
        for (u32 o = 0; o < 0x600; o += 4) mem::w32(player + o, 0);
        for (u32 o = 0; o < 0x200; o += 4) mem::w32(beam + o, 0);
        for (u32 o = 0; o < 0x280; o += 4) mem::w32(target + o, 0);
        mem::w32(player, kPlayerTable + 8);
        mem::w32(player + 0x210, 0x12);  // State::telekinesis
        mem::w32(player + 0x5b8, 1);     // TelekinesisState::active
        mem::w32(player + 0x5d8, beam);
        mem::w32(beam, kBeamTable + 8);
        mem::w32(beam + 0x174, target);
        mem::w32(beam + 0x1a8, 3);       // holding
        mem::w32(target, kTargetTable + 8);
        mem::w32(target + 0x28, 0x30);   // ordinary telekinesisable object type
    };
    reset_objects();

    // Unresolved symbols (before library load): unverified -> none, and retried once loaded.
    require(telekinesis::query_release(player).action == A::none, "unresolved symbols keep normal interact route");
    symbols = {
        {"_ZN18GameObjectPlayable16abortTelekinesisEv", kAbortFn},
        {"_ZNK18GameObjectPlayable20getTelekinesisTargetEv", kTargetFn},
        {"_ZN25GameObjectTelekinesisBeam18onAbortTelekinesisEv", kBeamAbortFn},
        {"_ZN18GameObjectPlayable16startTelekinesisEP21GameObjectInteractive", kStartFn},
        {"_ZTV18GameObjectPlayable", kPlayerTable},
        {"_ZTV25GameObjectTelekinesisBeam", kBeamTable},
    };

    // Holding an object (beam state 3): release via abortTelekinesis.
    {
        auto s = telekinesis::query_release(player);
        require(s.action == A::release && s.abort_fn == kAbortFn && s.target == target &&
                    s.tk_state == 1 && s.beam_state == 3,
                "holding target must trigger non-throw release");
    }

    // Pulling an object in (beam state 2): also safely releasable via abortTelekinesis.
    mem::w32(beam + 0x1a8, 2);
    {
        auto s = telekinesis::query_release(player);
        require(s.action == A::release && s.abort_fn == kAbortFn && s.target == target && s.beam_state == 2,
                "pulling target must also release cleanly");
    }

    // Acquisition and exit transitions must not release prematurely or fall through to grab/interact.
    mem::w32(beam + 0x1a8, 0);
    require(telekinesis::query_release(player).action == A::busy, "beam idle during telekinesis state is busy");
    mem::w32(beam + 0x1a8, 1);
    require(telekinesis::query_release(player).action == A::busy, "beam extending to target is busy, not releasable");
    mem::w32(beam + 0x1a8, 4);
    require(telekinesis::query_release(player).action == A::busy, "unknown beam state is busy");
    mem::w32(beam + 0x1a8, 3);

    for (u32 tk_state : {0u, 2u, 3u, 99u}) {
        mem::w32(player + 0x5b8, tk_state);
        auto s = telekinesis::query_release(player);
        require(s.action == A::busy && s.abort_fn == 0 && s.tk_state == tk_state,
                "arm-raise, throwing, already-aborting or unknown tk_state must report busy");
    }
    mem::w32(player + 0x5b8, 1);

    // Non-telekinesis player states pass straight through to normal interact::use().
    for (u32 state : {0u, 1u, 4u, 9u, 0x11u, 0x13u, 0xffffffffu}) {
        mem::w32(player + 0x210, state);
        require(telekinesis::query_release(player).action == A::none, "non-telekinesis states keep normal E behavior");
    }
    reset_objects();

    // Missing or invalid beam/target while in telekinesis state: busy (never dereference bad target).
    mem::w32(player + 0x5d8, 0);
    require(telekinesis::query_release(player).action == A::busy, "null beam pointer");
    mem::w32(player + 0x5d8, beam);
    mem::w32(beam, kBeamTable + 0x10);
    require(telekinesis::query_release(player).action == A::busy, "wrong beam vtable");
    mem::w32(beam, kBeamTable + 8);
    mem::w32(beam + 0x174, 0);
    require(telekinesis::query_release(player).action == A::busy, "null target pointer");
    mem::w32(beam + 0x174, target);
    mem::w32(target, 0x100);
    require(telekinesis::query_release(player).action == A::busy, "low/invalid target vtable");
    mem::w32(target, mem::ELF_BASE + 0x1aff80);
    require(telekinesis::query_release(player).action == A::busy, "partly mapped target vtable");
    reset_objects();

    // Socketed types 0x48 / 0x49 read +0x234 in onDropObject: require mapping through +0x234.
    for (u32 special : {0x48u, 0x49u}) {
        mem::w32(target + 0x28, special);
        require(telekinesis::query_release(player).action == A::release, "mapped special target is releasable");
        const u32 edge_target = mem::ELF_BASE + 0x1aff00;  // +0x28 mapped, +0x234 unmapped
        mem::w32(edge_target, kTargetTable + 8);
        mem::w32(edge_target + 0x28, special);
        mem::w32(beam + 0x174, edge_target);
        require(telekinesis::query_release(player).action == A::busy,
                "special target without mapped +0x234 must not be released");
        mem::w32(beam + 0x174, target);
    }
    reset_objects();

    // Read-only guarantee.
    (void)telekinesis::query_release(player);
    require(mem::r32(player + 0x210) == 0x12 && mem::r32(player + 0x5b8) == 1 &&
                mem::r32(beam + 0x174) == target && mem::r32(beam + 0x1a8) == 3,
            "query_release never mutates guest memory");

    // Wrong player vtable or bad player addresses -> none.
    mem::w32(player, kPlayerTable + 0x10);
    require(telekinesis::query_release(player).action == A::none, "wrong player vtable");
    mem::w32(player, kPlayerTable + 8);
    require(telekinesis::query_release(0).action == A::none, "null player");
    require(telekinesis::query_release(0xfffffff0).action == A::none, "overflowing player address");
    const u32 edge_player = mem::ELF_BASE + 0x1afd00;  // +0x210 mapped, +0x5d8 unmapped
    mem::w32(edge_player, kPlayerTable + 8);
    mem::w32(edge_player + 0x210, 0x12);
    require(telekinesis::query_release(edge_player).action == A::none, "partly mapped player");

    // Unverified guest code: any modified signature word disables the adapter.
    auto check_signature_words = [&](u32 base, const auto& words) {
        for (const telekinesis::Word& w : words) {
            mem::w32(base + w.offset, w.word ^ 1);
            require(telekinesis::query_release(player).action == A::none, "modified guest code must not be trusted");
            mem::w32(base + w.offset, w.word);
        }
    };
    check_signature_words(kAbortFn, telekinesis::kAbortWords);
    check_signature_words(kTargetFn, telekinesis::kTargetWords);
    check_signature_words(kBeamAbortFn, telekinesis::kBeamAbortWords);
    check_signature_words(kStartFn, telekinesis::kStartWords);
    require(telekinesis::query_release(player).action == A::release, "restored signatures work again");
}

}  // namespace

namespace elf {
u32 find(const std::string& name) {
    auto it = symbols.find(name);
    return it == symbols.end() ? 0 : it->second;
}
}  // namespace elf

int main() {
    try {
        run_tests();
        std::cout << "Telekinesis release query and signature tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
