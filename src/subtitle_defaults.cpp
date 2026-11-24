#include "subtitle_defaults.h"
#include "elf_loader.h"

namespace subtitle_defaults {
namespace {
struct Word { u32 offset, expected; };
constexpr Word kWords[] = {
    {0x1b7fb0, 0xe35a0000},  // cmp sl, #0: does the subtitle field exist?
    {0x1b7fb4, 0x1a000025},  // bne stored-value path (left untouched)
    {0x1b8010, 0x13a05001},  // missing field, different language length: already on
    {0x1b814c, 0xe2505000},  // subs r5, r0, #0: missing-field language comparison
    {0x1b8150, 0x13a05001},  // movne r5, #1 -> mov r5, #1
    {0x1b8154, 0xe0642002},
    {0x1bdc54, 0xe3590000},  // cmp sb, #0: reset's language comparison
    {0x1bdc58, 0x13a01000},  // movne r1, #0 -> movne r1, #1
    {0x1bdc5c, 0x03a01001},  // moveq r1, #1: other languages already on
    {0x1bdc60, 0xe1a00006},
    {0x1bdc64, 0xebfffac6},  // bl Settings::setSubtitlesEnabled
};
}  // namespace

bool install() {
    if (elf::find("_ZNK8Settings19areSubtitlesEnabledEv") != mem::ELF_BASE + 0x1b7ed8 ||
        elf::find("_ZN8Settings26resetSettingsNotBrightnessEv") != mem::ELF_BASE + 0x1bd924 ||
        elf::find("_ZN8Settings19setSubtitlesEnabledEa") != mem::ELF_BASE + 0x1bc784) return false;
    // Validate both sites before writing either. No profile access or startup setter call:
    // the existing-field branch still reads the player's saved On/Off choice normally.
    for (const auto& w : kWords)
        if (!mem::range_mapped(mem::ELF_BASE + w.offset, 4) ||
            mem::r32(mem::ELF_BASE + w.offset) != w.expected) return false;
    mem::w32(mem::ELF_BASE + 0x1b8150, 0xe3a05001);
    mem::w32(mem::ELF_BASE + 0x1bdc58, 0x13a01001);
    return true;
}
}  // namespace subtitle_defaults
