#include "solver_init.h"
#include "cpu.h"
#include "elf_loader.h"
#include "hle/hle.h"
#include "memory.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
void require(bool b, const char* why) { if (!b) throw std::runtime_error(why); }
constexpr const char* setup = "_ZN35btSequentialImpulseConstraintSolver28solveGroupCacheFriendlySetupEPP17btCollisionObjectiPP20btPersistentManifoldiPP17btTypedConstraintiRK19btContactSolverInfoP12btIDebugDrawP12btStackAlloc";
constexpr const char* search = "_ZN2EA4StdC6MemchrEPKccj";
constexpr u32 stub = mem::STUB_BASE + 8, call = mem::ELF_BASE + 0x26325c;
constexpr std::pair<u32,u32> words[] = {
    {0x262fe8,0xe92d4ff0},{0x262fec,0xed2d8b0c},{0x263250,0xe1a00006},{0x263254,0xe3a01000},
    {0x263258,0xe3a02088},{0x26325c,0xeb00a0a0},{0x263260,0xed869a1f},{0x263264,0xedc69a20},
    {0x263268,0xed868a15},{0x26326c,0xed868a14},{0x263270,0xe5864068},{0x263274,0xe586b06c},
    {0x2632c0,0xe289e07c},{0x28b4e4,0xe3520000},{0x28b4e8,0x0a00000a}
};
std::unordered_map<std::string,u32> symbols;
u32 resolved = stub; unsigned lookups = 0; std::string stub_name = "memset";
std::vector<u32> identity_queries;
void seed() {
    symbols = {{setup,mem::ELF_BASE+0x262fe8},{search,mem::ELF_BASE+0x28b4e4}};
    for (auto [at,w] : words) mem::w32(mem::ELF_BASE+at,w);
    mem::w32(stub,0xef000007); mem::w32(stub+4,0xe12fff1e);
    // The other search call must remain untouched.
    mem::w32(mem::ELF_BASE+0x26153c,0xeb00a7e8);
    resolved=stub; lookups=0; stub_name="memset"; identity_queries.clear();
}
void test() {
    require(!solver::install_joint_row_clear(),"unloaded symbols do not access guest memory");
    require(mem::init(),"synthetic guest reservation");
    mem::commit(mem::ELF_BASE,0x290000); mem::commit(mem::STUB_BASE,mem::PAGE_SIZE); seed();
    for (const char* name : {setup,search}) {
        const auto saved=symbols[name]; symbols[name]=0;
        require(!solver::install_joint_row_clear() && mem::r32(call)==0xeb00a0a0 && !lookups,"missing symbol leaves original call and HLE registry untouched");
        symbols[name]=saved;
    }
    for (auto [at,w] : words) {
        mem::w32(mem::ELF_BASE+at,w^1);
        require(!solver::install_joint_row_clear() && !lookups,"every instruction guard precedes HLE lookup/write");
        mem::w32(mem::ELF_BASE+at,w);
    }
    for (u32 address : {0u,stub+1,mem::STUB_BASE,mem::STUB_BASE+mem::STUB_SIZE,0x50000000u}) {
        resolved=address; require(!solver::install_joint_row_clear() && mem::r32(call)==0xeb00a0a0,"bad stub never patches call");
    }
    require(identity_queries.empty(),"out-of-range/misaligned stubs rejected before identity lookup");
    // mem::init commits the ENTIRE stub region. An unused slot in its third
    // page is mapped but has no registered handler, not an unmapped address.
    const u32 unused=mem::STUB_BASE+2*mem::PAGE_SIZE;
    require(mem::range_mapped(unused,8),"unused fixture slot really is mapped");
    resolved=unused;
    require(!solver::install_joint_row_clear() && mem::r32(call)==0xeb00a0a0,"mapped unnamed stub rejected without patching");
    require(identity_queries.size()==1 && identity_queries.back()==unused,"mapped unused slot rejected by handler identity");
    // Inject a genuine mapping-check failure, then restore the fixture page.
    auto& page=(*mem::g_page_table)[unused>>mem::PAGE_BITS]; const auto saved_page=page;
    page=nullptr;
    require(!mem::range_mapped(unused,8),"injected unmapped stub page");
    const auto queries_before=identity_queries.size();
    const bool installed_unmapped=solver::install_joint_row_clear(); page=saved_page;
    require(!installed_unmapped && mem::r32(call)==0xeb00a0a0,"genuinely unmapped stub rejected without patching");
    require(identity_queries.size()==queries_before,"unmapped stub rejected before handler lookup");
    resolved=stub; stub_name="unrelated";
    require(!solver::install_joint_row_clear(),"wrong handler identity rejected"); stub_name="memset";
    for (u32 bad : {0xe1a00000u,0xef000000u,0xefffffffu}) {
        mem::w32(stub,bad); require(!solver::install_joint_row_clear() && mem::r32(call)==0xeb00a0a0,"invalid/return SVC rejected");
    }
    mem::w32(stub,0xef000007); mem::w32(stub+4,0xe1a00000);
    require(!solver::install_joint_row_clear(),"malformed HLE stub rejected"); mem::w32(stub+4,0xe12fff1e);
    std::vector<u8> before(0x290000); std::memcpy(before.data(),mem::host(mem::ELF_BASE),before.size());
    require(solver::install_joint_row_clear(),"supported setup patched");
    for (u32 i=0;i<before.size();++i) {
        if (i>=0x26325c && i<0x263260) continue;
        require(mem::r8(mem::ELF_BASE+i)==before[i],"only the joint-init call instruction may change");
    }
    require(mem::r32(call)==0xef000007,"inline only SVC, no BX LR or hook trampoline");
}
}
namespace elf { u32 find(const std::string& n) { auto i=symbols.find(n); return i==symbols.end()?0:i->second; } }
namespace hle {
u32 resolve(const std::string& n,bool function,bool weak) {
    require(n=="memset" && function && weak,"only weak existing memset lookup permitted"); ++lookups; return resolved;
}
}
namespace cpu {
std::string hle_name_at(u32 address) {
    require(address>=mem::STUB_BASE+8 && address<=mem::STUB_BASE+mem::STUB_SIZE-8 &&
            !(address&7) && mem::range_mapped(address,8),"only validated mapped slots reach identity lookup");
    identity_queries.push_back(address);
    return address==stub ? stub_name : std::string{}; // unused slots have no HLE name
}
}
namespace logging { void write(char,const char*,...) {} }
int main() { try { test(); std::cout<<"Joint-row initialization installer tests passed; no game executed\n"; return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
