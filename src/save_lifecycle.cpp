// Observe native serialization boundaries without replacing the serializer,
// suppressing cleanup, changing the save format or inventing a recovery slot.
#include "save_lifecycle.h"
#include "save_io.h"
#include "cpu.h"
#include "elf_loader.h"
#include "log.h"
#include <vector>
#include <cstdlib>

namespace save_lifecycle {
namespace {
enum class Operation { save, load, close, flush };
struct Frame {
    Operation operation;
    u32 link;
    uint64_t failures;
    bool already_failed = false;
    std::string path;
};
thread_local std::vector<Frame> frames;
u32 return_stub = 0;
u32 file_input_vtable = 0;
void enter(cpu::Ctx& c, Operation operation, const std::string& path = {}, bool failed = false) {
    frames.push_back({operation, c.lr(), save_io::failure_epoch(), failed, path});
    c.set_reg(14, return_stub);
}
void on_save(cpu::Ctx& c) {
    save_io::begin_save();
    enter(c, Operation::save);
    LOGD("[save-io] native filename serialization BEGIN (per-file publication deferred until result/close)");
}
void on_load(cpu::Ctx& c) {
    u32 stream = c.reg(1);
    if (!mem::range_mapped(stream, 8) || mem::r32(stream) != file_input_vtable) return;
    u32 file = mem::r32(stream + 4);
    if (!mem::range_mapped(file, 0x14)) return;
    auto info = save_io::load_info(int(mem::r32(file + 0x10)));
    if (info.path.empty()) return;  // correlate to this stream's fd, not a recent unrelated open
    if (info.structurally_invalid) {
        LOGE("[save-load] refusing structurally invalid selected file path=%s before native parsing; see validation reason above; port performed no repair/fallback/deletion", info.path.c_str());
        cpu::skip_function(c, 0);  // caller still owns and closes its stream normally
        return;
    }
    enter(c, Operation::load, info.path);
    LOGD("[save-load] native serialization parse BEGIN path=%s", info.path.c_str());
}
void on_close(cpu::Ctx& c) {
    if (!mem::range_mapped(c.reg(0), 0x14)) return;
    int fd = int(mem::r32(c.reg(0) + 0x10));
    if (save_io::tracked_profile_fd(fd)) enter(c, Operation::close, {}, save_io::failed_fd(fd));
}
void on_flush(cpu::Ctx& c) {
    if (!mem::range_mapped(c.reg(0), 0x14)) return;
    int fd = int(mem::r32(c.reg(0) + 0x10));
    if (save_io::tracked_profile_fd(fd)) enter(c, Operation::flush, {}, save_io::failed_fd(fd));
}
void on_return(cpu::Ctx& c) {
    if (frames.empty()) {
        LOGE("[save-io] serialization return context missing");
        std::abort();  // never fabricate a return address or suppress native cleanup
    }
    Frame frame = std::move(frames.back());
    frames.pop_back();
    bool result = (c.reg(0) & 0xff) != 0;
    if (frame.operation == Operation::save) {
        bool committed = save_io::finish_save(result);
        if (!result || !committed)
            LOGE("[save-io] native filename serialization END serialized=%s published=%s", result ? "ok" : "FAILED", committed ? "ok" : "FAILED");
        else
            LOGD("[save-io] native filename serialization END serialized=ok published=ok");
        if (!committed) c.ret(0);
    } else if (frame.operation == Operation::load) {
        if (!result)
            LOGE("[save-load] native serialization parse END path=%s result=FAILED; inspect selected-file validation and I/O diagnostics above; no automatic port fallback/repair performed", frame.path.c_str());
        else
            LOGD("[save-load] native serialization parse END path=%s result=ok; state restoration, assets and rendering are subsequent stages, not validated by this result", frame.path.c_str());
    } else if (frame.already_failed || frame.failures != save_io::failure_epoch()) {
        c.ret(0);  // native FileStream close/flush otherwise discard imported errors
        LOGE("[save-io] native FileStream %s result corrected to failure after host I/O error", frame.operation == Operation::close ? "close" : "flush");
    }
    c.set_reg(14, frame.link);
}
}
void install() {
    const struct { const char* name; u32 offset, first, second; cpu::HleFn callback; } hooks[] = {
        {"_ZN2im13serialization8Database4saveERKN5eastl12basic_stringIwNS_20StringEASTLAllocatorEEE", 0x33ac90, 0xe92d40f0, 0xe24dd024, on_save},
        {"_ZN2im13serialization8Database4loadERNS_12IInputStreamE", 0x33b208, 0xe92d40f0, 0xe1a04000, on_load},
        {"_ZN2EA2IO10FileStream5CloseEv", 0x271624, 0xe92d4030, 0xe59030a4, on_close},
        {"_ZN2EA2IO10FileStream5FlushEv", 0x2716b4, 0xe92d4010, 0xe5900010, on_flush},
    };
    for (const auto& hook : hooks) {
        u32 fn = elf::find(hook.name);
        if (fn != mem::ELF_BASE + hook.offset || mem::r32(fn) != hook.first || mem::r32(fn + 4) != hook.second) {
            LOGW("[save-io] native serialization boundary unsupported: %s; checked atomic I/O remains active, publication uses validated close", hook.name);
            return;
        }
    }
    u32 input = elf::find("_ZTVN2im15FileInputStreamE");
    u32 read = elf::find("_ZN2im15FileInputStream4readEPci");
    if (!input || read != mem::ELF_BASE + 0x383418 || mem::r32(read + 4) != 0xe5903004) {
        LOGW("[save-io] profile input-stream layout unsupported; validated-close atomic I/O remains active");
        return;
    }
    file_input_vtable = input + 8;
    return_stub = cpu::register_hle("profile serialization return", on_return);
    for (const auto& hook : hooks)
        if (!cpu::hook_entry(mem::ELF_BASE + hook.offset, hook.name, hook.callback)) {
            LOGE("[save-io] cannot install verified serialization boundary %s", hook.name);
            std::abort();  // a partial set of return/scope hooks is not safe to run
        }
    LOGD("[save-io] native serialization/close/flush result tracking installed; atomic profile saves enabled");
}
}  // namespace save_lifecycle
