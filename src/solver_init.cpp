#include "solver_init.h"

#include "cpu.h"
#include "elf_loader.h"
#include "hle/hle.h"
#include "log.h"
#include "memory.h"

namespace solver {
bool install_joint_row_clear() {
    // The original call passes (row, 0, sizeof(btSolverConstraint)), but its
    // target searches bytes instead of clearing them and its return is unused.
    // Point-joint getInfo2 writes diagonal 1s, relying on cleared off-diagonals
    // and CFM. Reused row contents can therefore poison the solver inputs.
    if (elf::find("_ZN35btSequentialImpulseConstraintSolver28solveGroupCacheFriendlySetupEPP17btCollisionObjectiPP20btPersistentManifoldiPP17btTypedConstraintiRK19btContactSolverInfoP12btIDebugDrawP12btStackAlloc") != mem::ELF_BASE + 0x262fe8 ||
        elf::find("_ZN2EA4StdC6MemchrEPKccj") != mem::ELF_BASE + 0x28b4e4) {
        LOGW("solver row initialization: unsupported library symbols; fix not installed");
        return false;
    }
    const struct { u32 offset, expected; } guards[] = {
        {0x262fe8, 0xe92d4ff0}, {0x262fec, 0xed2d8b0c}, // supported setup prologue
        {0x263250, 0xe1a00006}, {0x263254, 0xe3a01000}, // MOV r0,r6; MOV r1,#0
        {0x263258, 0xe3a02088}, {0x26325c, 0xeb00a0a0}, // MOV r2,#136; BL Memchr
        {0x263260, 0xed869a1f}, {0x263264, 0xedc69a20}, // ordinary builder resumes here
        {0x263268, 0xed868a15}, {0x26326c, 0xed868a14},
        {0x263270, 0xe5864068}, {0x263274, 0xe586b06c},
        {0x2632c0, 0xe289e07c},                       // LR redefined before use
        {0x28b4e4, 0xe3520000}, {0x28b4e8, 0x0a00000a}, // original search helper
    };
    for (const auto& g : guards) {
        const u32 address = mem::ELF_BASE + g.offset;
        if (!mem::range_mapped(address, 4) || mem::r32(address) != g.expected) {
            LOGW("solver row initialization: unsupported instruction at +%x; fix not installed", g.offset);
            return false;
        }
    }
    // Weak lookup never invents an unimplemented import if registration is absent.
    const u32 stub = hle::resolve("memset", true, true);
    if (stub < mem::STUB_BASE + 8 || stub > mem::STUB_BASE + mem::STUB_SIZE - 8 ||
        (stub & 7) || !mem::range_mapped(stub, 8) || cpu::hle_name_at(stub) != "memset" ||
        mem::r32(stub + 4) != 0xe12fff1e) {
        LOGW("solver row initialization: memset HLE stub unavailable; fix not installed");
        return false;
    }
    const u32 svc = mem::r32(stub);
    if ((svc & 0xff000000) != 0xef000000 || !(svc & 0xffffff) || (svc & 0xffffff) == 0xffffff) {
        LOGW("solver row initialization: invalid memset SVC; fix not installed");
        return false;
    }
    // Inline only the existing handler's SVC, NOT its BX LR. CallSVC resumes at
    // the following instruction, so row construction remains unchanged. The old
    // BL's LR/flags/r3 result is dead here; LR is redefined at +2632c0 before use.
    // HLE memset consumes r0/r1/r2, clears exactly 136 bytes and returns r0=row.
    // No global Memchr patch, late-row sweep, clamp or diagnostic frame dependency.
    mem::w32(mem::ELF_BASE + 0x26325c, svc);
    LOGI("solver row initialization: corrected joint-row Memchr call to memset (136 bytes)");
    return true;
}
} // namespace solver
