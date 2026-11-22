#include "limb_use.h"
#include "elf_loader.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
using E = interact::Outcome;
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
std::unordered_map<std::string, u32> symbols;
struct Candidate {
    bool selectable, in_hitbox;
    E outcome;
};

// The reported case: ammo dropped on a corpse, with several limbs nearer the line of sight than the ammo.
void selection() {
    std::vector<Candidate> c{{true, true, E::declines}, {true, true, E::declines}, {true, true, E::declines},
                             {true, true, E::acts}};
    require(interact::best_selectable(c) == &c[3], "ammo is chosen over the limbs scoring ahead of it");
    c[3].selectable = false;
    require(interact::best_selectable(c) == &c[0], "with only limbs in reach the first limb keeps E, as before");
    c[3] = {true, true, E::unverified};
    require(interact::best_selectable(c) == &c[3], "any other usable object outranks a limb");
    c = {{true, true, E::declines}, {true, true, E::declines}};
    require(interact::best_selectable(c) == &c[0], "limbs keep their own order among themselves");
    c = {{true, true, E::declines}, {true, false, E::acts}};
    require(interact::best_selectable(c) == &c[0], "an out-of-reach object is not chosen over a limb");
}

void adapter() {
    require(limb::use_outcome() == E::unverified, "symbols not loaded yet: nothing is known");
    symbols = {{"_ZN14GameObjectLimb8onUpdateEi", 0x1000}};
    require(limb::use_outcome() == E::declines, "limbs have no onOpen of their own: E has no effect");
    symbols["_ZN14GameObjectLimb6onOpenEv"] = 0x2000;
    require(limb::use_outcome() == E::unverified, "a limb with its own onOpen keeps the original behaviour");
    symbols.erase("_ZN14GameObjectLimb6onOpenEv");
    require(limb::use_outcome() == E::declines, "nothing is cached: the answer follows the symbols");
    symbols.clear();
    require(limb::use_outcome() == E::unverified, "symbols gone again: unverified");
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
        selection();
        adapter();
        std::cout << "Limb selection and adapter tests passed; no game executed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
