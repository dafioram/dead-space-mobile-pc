#include "limb_cleanup_rules.h"
#include "elf_loader.h"

namespace limb_cleanup {
namespace {
struct Word {
    u32 offset, word;
};
// Instruction words of GameObject::destroy that the assumptions above rest on, checked on every query.
constexpr Word kDestroyWords[] = {
    {0x04, 0xe5903048},  // ldr  r3, [r0, #0x48]   flag word
    {0x10, 0xe2131001},  // ands r1, r3, #1        already destroyed?
    {0x14, 0x0a000001},  // beq  -> do the work (else return)
    {0x28, 0xe5803048},  // str  r3, [r0, #0x48]   set the destroyed bit
};
}  // namespace

u32 destroy_function() {
    const u32 fn = elf::find("_ZN10GameObject7destroyEv");
    if (fn != mem::ELF_BASE + 0x17dfdc) return 0;
    for (const Word& w : kDestroyWords)
        if (!mem::range_mapped(fn + w.offset, 4) || mem::r32(fn + w.offset) != w.word) return 0;
    return fn;
}
}  // namespace limb_cleanup
