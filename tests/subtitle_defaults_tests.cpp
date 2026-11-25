#include "subtitle_defaults.h"
#include "elf_loader.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
std::unordered_map<std::string, u32> symbols;
struct Word { u32 offset, value; };
// Sparse synthetic signature words only, never an executable game fixture.
constexpr Word words[] = {
    {0x1b7fb0, 0xe35a0000}, {0x1b7fb4, 0x1a000025}, {0x1b8010, 0x13a05001},
    {0x1b814c, 0xe2505000}, {0x1b8150, 0x13a05001}, {0x1b8154, 0xe0642002},
    {0x1bdc54, 0xe3590000}, {0x1bdc58, 0x13a01000}, {0x1bdc5c, 0x03a01001},
    {0x1bdc60, 0xe1a00006}, {0x1bdc64, 0xebfffac6},
};
void seed() { for (auto w : words) mem::w32(mem::ELF_BASE + w.offset, w.value); }
void unchanged() {
    for (auto w : words) require(mem::r32(mem::ELF_BASE + w.offset) == w.value, "failed install must not partially patch");
}

// Evaluate just the immediate MOV encodings used here, including their condition.
// This is not guest/JIT execution; it checks both comparison outcomes explicitly.
u32 move(u32 instruction, bool equal, u32 before) {
    require((instruction & 0x0fffff00) == 0x03a05000 || (instruction & 0x0fffff00) == 0x03a01000,
            "expected a non-flag-setting immediate MOV");
    const u32 cond = instruction >> 28;
    require(cond == 0 || cond == 1 || cond == 14, "expected EQ, NE or AL");
    return (cond == 14 || (cond == 0 && equal) || (cond == 1 && !equal)) ? instruction & 0xff : before;
}
void test() {
    require(mem::init(), "synthetic memory reservation");
    symbols = {
        {"_ZNK8Settings19areSubtitlesEnabledEv", mem::ELF_BASE + 0x1b7ed8},
        {"_ZN8Settings26resetSettingsNotBrightnessEv", mem::ELF_BASE + 0x1bd924},
        {"_ZN8Settings19setSubtitlesEnabledEa", mem::ELF_BASE + 0x1bc784},
    };
    require(!subtitle_defaults::install(), "unmapped library rejected without reading it");
    mem::commit(mem::ELF_BASE + 0x1b7000, 0x7000);
    seed();
    for (auto& [name, address] : symbols) {
        const u32 original = address;
        for (u32 bad : {0u, original + 4}) {
            address = bad;
            require(!subtitle_defaults::install(), "missing or shifted symbol rejected");
            unchanged();
        }
        address = original;
    }
    for (auto w : words) {
        mem::w32(mem::ELF_BASE + w.offset, w.value ^ 1);
        require(!subtitle_defaults::install(), "every signature word checked before either write");
        mem::w32(mem::ELF_BASE + w.offset, w.value);
        unchanged();
    }
    require(subtitle_defaults::install(), "recognized library patched");
    for (auto w : words) {
        u32 expected = w.value;
        if (w.offset == 0x1b8150) expected = 0xe3a05001;
        if (w.offset == 0x1bdc58) expected = 0x13a01001;
        require(mem::r32(mem::ELF_BASE + w.offset) == expected, "only the two default MOVs change");
    }
    for (bool equal : {false, true}) {
        require(move(mem::r32(mem::ELF_BASE + 0x1b8150), equal, 0) == 1,
                "absent subtitle field defaults on for equal/different language text");
        u32 reset = move(mem::r32(mem::ELF_BASE + 0x1bdc58), equal, 99);
        reset = move(mem::r32(mem::ELF_BASE + 0x1bdc5c), equal, reset);
        require(reset == 1, "reset defaults on for both language comparison outcomes");
    }
    // The guarded hasField BNE remains unchanged, bypassing all modified fallback
    // instructions for BOTH explicit saved values. No setter or save I/O is called.
    const u32 branch = mem::r32(mem::ELF_BASE + 0x1b7fb4);
    require(branch == 0x1a000025 && 0x1b7fb4 + 8 + (branch & 0xffffff) * 4 == 0x1b8050,
            "stored On and Off still branch directly to the original field-reading path");
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
        test();
        std::cout << "Subtitle default patch tests passed; no game or profile loaded\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
