#include "hle/hle.h"
#include "hle/io_buffer.h"
#include "save_io.h"
#include <cerrno>
#include <fcntl.h>

namespace hle {
namespace {
int host_open_flags(u32 guest) {
    int flags = (guest & 3) == 0 ? O_RDONLY : (guest & 3) == 1 ? O_WRONLY : O_RDWR;
    if (guest & 0x40) flags |= O_CREAT;
    if (guest & 0x80) flags |= O_EXCL;
    if (guest & 0x200) flags |= O_TRUNC;
    if (guest & 0x400) flags |= O_APPEND;
    return flags;
}
}
void register_file_descriptors() {
    reg("open", [](Ctx& c) {
        auto path = save_io::utf8_path(host_path(c.argstr(0)));
        int fd = save_io::open(path, host_open_flags(c.arg(1)), 0644);
        if (fd < 0) set_errno(host_errno_to_guest(errno));
        c.ret(u32(fd));
    });
    reg("close", [](Ctx& c) {
        int result = save_io::close(int(c.arg(0)));
        if (result < 0) set_errno(host_errno_to_guest(errno));
        c.ret(u32(result));
    });
    reg("read", [](Ctx& c) {
        if (!io_buffer(c.arg(1), c.arg(2))) {
            save_io::fail_fd(int(c.arg(0)), "invalid guest read destination", EFAULT);
            set_errno(G_EFAULT); return c.ret(0xffffffff);
        }
        int64_t result = save_io::read(int(c.arg(0)), mem::host(c.arg(1)), c.arg(2));
        if (result < 0) set_errno(host_errno_to_guest(errno));
        c.ret(u32(result));
    });
    reg("write", [](Ctx& c) {
        if (!io_buffer(c.arg(1), c.arg(2))) {
            save_io::fail_fd(int(c.arg(0)), "invalid guest write source", EFAULT);
            set_errno(G_EFAULT); return c.ret(0xffffffff);
        }
        int64_t result = save_io::write(int(c.arg(0)), mem::host(c.arg(1)), c.arg(2));
        if (result < 0) set_errno(host_errno_to_guest(errno));
        c.ret(u32(result));
    });
    reg("lseek", [](Ctx& c) {
        int64_t result = save_io::seek(int(c.arg(0)), s32(c.arg(1)), int(c.arg(2)));
        if (result < 0) set_errno(host_errno_to_guest(errno));
        else if (result > INT32_MAX) { set_errno(G_EOVERFLOW); return c.ret(0xffffffff); }
        c.ret(u32(result));
    });
    reg("fsync", [](Ctx& c) {
        int result = save_io::sync(int(c.arg(0)));
        if (result < 0) set_errno(host_errno_to_guest(errno));
        c.ret(u32(result));
    });
    reg("ftruncate", [](Ctx& c) {
        if (s32(c.arg(1)) < 0) {
            save_io::fail_fd(int(c.arg(0)), "negative ftruncate size", EINVAL);
            set_errno(G_EINVAL); return c.ret(0xffffffff);
        }
        int result = save_io::truncate(int(c.arg(0)), c.arg(1));
        if (result < 0) set_errno(host_errno_to_guest(errno));
        c.ret(u32(result));
    });
}
}  // namespace hle
