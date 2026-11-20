#include "ammo_pickup.h"
#include "cpu.h"
#include "elf_loader.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
std::unordered_map<std::string, u32> symbols;
u32 object, player, inventory;
unsigned calls = 0;
bool accepts = false;
struct Candidate {
    bool usable, overlap;
    pickup::Eligibility ammo;
};
void selection() {
    using E = pickup::Eligibility;
    std::vector<Candidate> c{{true,true,E::blocked},{true,true,E::unknown},{true,true,E::available}};
    require(pickup::reachable(c) == &c[1], "full ammo must not block terminal in existing score order");
    c[1].ammo = E::blocked;
    require(pickup::reachable(c) == &c[2], "multiple full pickups must not block collectable ammo");
    c[0].ammo = E::available;
    require(pickup::reachable(c) == &c[0], "partial/upgraded capacity keeps original ranking");
    c[0].ammo = E::unknown;
    require(pickup::reachable(c) == &c[0], "unknown layout preserves existing behavior");
    for (auto& item : c) item.ammo = E::blocked;
    require(pickup::reachable(c) == &c[0], "all full preserves first refusal target, not distant kinesis");
    c[1].overlap = false; c[2].usable = false;
    require(pickup::reachable(c) == &c[0], "do not select unreachable or disabled alternatives");
    c[0].usable = false;
    require(!pickup::reachable(c), "no eligible overlap leaves ordinary kinesis decision unchanged");
}
void adapter() {
    require(mem::init(), "reserve synthetic memory; no guest/JIT execution");
    mem::commit(mem::ELF_BASE, 0x140000);
    symbols = {{"_ZNK9Inventory10canAddItemE4ItemPK18GameObjectPlayable",mem::ELF_BASE+0x9b160},
               {"_ZTV16GameObjectPickup",mem::ELF_BASE+0x1000},
               {"_ZN16GameObjectPickup6onOpenEv",mem::ELF_BASE+0x13027c},
               {"_ZN18GameObjectPlayable6pickupER4Itema",mem::ELF_BASE+0x193524}};
    // Signature-only synthetic words, not a copied game routine or executable fixture.
    mem::commit(mem::ELF_BASE+0x193000, 0x1000);
    mem::w32(mem::ELF_BASE+0x1302a8,0xe2841f7b);
    mem::w32(mem::ELF_BASE+0x19368c,0xe5950530);
    mem::w32(mem::ELF_BASE+0x1936ac,0xebfc1eab);
    mem::w32(mem::ELF_BASE+0x9b160,0xe92d47f0);
    mem::w32(mem::ELF_BASE+0x9b180,0xe5953004);
    mem::w32(mem::ELF_BASE+0x9b244,0xe5951004);
    object = mem::heap_alloc(0x300); player = mem::heap_alloc(0x600); inventory = mem::heap_alloc(0x30);
    mem::w32(object,mem::ELF_BASE+0x1008);
    mem::w32(object+0x10,0xabc); mem::w32(player+0x10,0xabc);
    mem::w32(player+0x530,inventory);
    mem::w32(object+0x1ec,mem::ELF_BASE+0x2008);
    mem::w32(object+0x1f0,7); mem::w32(object+0x1f4,20); mem::w32(object+0x1f8,25);
    require(pickup::ammo_eligibility(object,player)==pickup::Eligibility::blocked && calls==1,"native refusal");
    accepts = true;
    require(pickup::ammo_eligibility(object,player)==pickup::Eligibility::available && calls==2,"partial capacity query result");
    // Re-query each selection: shooting/reloading/upgrades must not leave a cached refusal.
    accepts = false;
    require(pickup::ammo_eligibility(object,player)==pickup::Eligibility::blocked && calls==3,"no stale eligibility cache");
    require(mem::r32(object+0x1f4)==20,"query scratch never aliases original pickup");
    mem::w32(object+0x1f0,14);
    require(pickup::ammo_eligibility(object,player)==pickup::Eligibility::unknown && calls==3,"nodes/credits/weapons untouched");
    mem::w32(object+0x1f0,7); mem::w32(player+0x10,0xdef);
    require(pickup::ammo_eligibility(object,player)==pickup::Eligibility::unknown,"wrong scene is not queried");
    mem::w32(player+0x10,0xabc); mem::w32(mem::ELF_BASE+0x1302a8,0);
    require(pickup::ammo_eligibility(object,player)==pickup::Eligibility::unknown && calls==3,"unverified layout is not queried");
    require(pickup::ammo_eligibility(0xfffffff0,player)==pickup::Eligibility::unknown,"bad range safe");
}
}  // namespace
namespace elf { u32 find(const std::string& name) { auto it=symbols.find(name);return it==symbols.end()?0:it->second; } }
namespace cpu {
u32 call(u32 fn, std::initializer_list<u32> args) {
    require(fn==mem::ELF_BASE+0x9b160 && args.size()==3,"only canAddItem may be called");
    auto it=args.begin();require(*it++==inventory,"inventory ABI");u32 copy=*it++;
    require(*it==player && copy!=object+0x1ec,"player ABI and by-value copy");
    require(mem::r32(copy+4)==7 && mem::r32(copy+8)==20 && mem::r32(copy+12)==25,"actual pickup payload copied");
    mem::w32(copy+8,0); ++calls; return accepts?1:0;
}
}
int main() { try { selection();adapter();std::cout<<"Ammo selection and native-query adapter tests passed; no game executed\n";return 0; }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;} }
