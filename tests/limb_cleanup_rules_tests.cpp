#include "limb_cleanup_rules.h"
#include "elf_loader.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
std::unordered_map<std::string, u32> symbols;
constexpr u32 kDestroy = mem::ELF_BASE + 0x17dfdc;
constexpr u32 kWords[][2] = {{0x04, 0xe5903048}, {0x10, 0xe2131001}, {0x14, 0x0a000001}, {0x28, 0xe5803048}};

void rules() {
    using limb_cleanup::broken;
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float pos[3] = {-61.22f, 2.20f, -114.81f}, lo[3] = {-61.42f, 2.00f, -115.01f}, hi[3] = {-61.02f, 2.40f, -114.61f};
    require(!broken(pos, lo, hi), "an ordinary limb is fine");
    const float zero[3] = {0, 0, 0};
    require(!broken(zero, zero, zero), "a limb at the origin with an empty box is not broken");
    // the cases seen in the dumps: NaN position and box, NaN box at a finite position, a 3e7 m position
    const float nan3[3] = {nan, nan, nan};
    require(broken(nan3, nan3, nan3), "NaN position and box");
    require(broken(pos, nan3, nan3), "finite position, NaN box (seen at z=-156)");
    const float far[3] = {31904058.0f, 17127726.0f, 62343532.0f};
    require(broken(far, nan3, nan3), "position of 3e7 m");
    require(broken(far, lo, hi), "an absurd position alone is enough");
    for (int i = 0; i < 3; ++i) {  // every single component of position, lo and hi is checked
        float p[3] = {pos[0], pos[1], pos[2]}, l[3] = {lo[0], lo[1], lo[2]}, h[3] = {hi[0], hi[1], hi[2]};
        p[i] = nan; require(broken(p, lo, hi), "NaN in one position component");
        l[i] = nan; require(broken(pos, l, hi), "NaN in one lo component");
        h[i] = inf; require(broken(pos, lo, h), "inf in one hi component");
        p[i] = pos[i]; l[i] = -inf; require(broken(pos, l, hi), "-inf in one lo component");
    }
    const float edge[3] = {1000.0f, -1000.0f, 1000.0f}, over[3] = {1000.01f, 0, 0};
    require(!broken(edge, lo, hi), "exactly the limit is still accepted");
    require(broken(over, lo, hi), "just past the limit is not");
    const float far_neg[3] = {-31904058.0f, 0, 0}, under[3] = {-1000.01f, 0, 0};
    require(broken(far_neg, lo, hi), "a huge negative position is not real either");
    require(broken(pos, far_neg, hi) && broken(pos, lo, far_neg), "a huge negative bound is not real either");
    require(broken(under, lo, hi), "just past the negative limit");
}

void destroy_verification() {
    require(mem::init(), "reserve synthetic memory; no guest/JIT execution");
    mem::commit(mem::ELF_BASE, 0x200000);
    require(limb_cleanup::destroy_function() == 0, "symbols not loaded yet: not verified");
    // Signature-only synthetic words, not a copied game routine or executable fixture.
    for (const auto& w : kWords) mem::w32(kDestroy + w[0], w[1]);
    symbols = {{"_ZN10GameObject7destroyEv", kDestroy}};
    require(limb_cleanup::destroy_function() == kDestroy, "verified core: destroy is usable");
    for (const auto& w : kWords) {  // every verified word must match
        mem::w32(kDestroy + w[0], w[1] ^ 1);
        require(limb_cleanup::destroy_function() == 0, "a changed destroy word is not trusted");
        mem::w32(kDestroy + w[0], w[1]);
    }
    require(limb_cleanup::destroy_function() == kDestroy, "code restored");
    // a complete, valid-looking signature at some other address is still not the verified core
    constexpr u32 kElsewhere = mem::ELF_BASE + 0x180000;
    for (const auto& w : kWords) mem::w32(kElsewhere + w[0], w[1]);
    symbols["_ZN10GameObject7destroyEv"] = kElsewhere;
    require(limb_cleanup::destroy_function() == 0, "destroy at another address is not the verified one");
    symbols.clear();
    require(limb_cleanup::destroy_function() == 0, "symbol gone: unknown");
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
        rules();
        destroy_verification();
        std::cout << "Limb cleanup rules and destroy verification tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
