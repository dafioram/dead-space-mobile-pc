#include "hle/hle.h"
#include <cerrno>
namespace hle {
int host_errno_to_guest(int error) {
    switch (error) {
    case ENOENT: return G_ENOENT;
    case EACCES: return G_EACCES;
    case EEXIST: return G_EEXIST;
    case ENOTDIR: return G_ENOTDIR;
    case EISDIR: return G_EISDIR;
    case EINVAL: return G_EINVAL;
    case ENOSPC: return G_ENOSPC;
    case EBADF: return G_EBADF;
    case ENOMEM: return G_ENOMEM;
    case EAGAIN: return G_EAGAIN;
    case EBUSY: return G_EBUSY;
    case ENOTEMPTY: return G_ENOTEMPTY;
    case EINTR: return G_EINTR;
    case EPERM: return G_EPERM;
    case EFAULT: return G_EFAULT;
    case EFBIG: return G_EFBIG;
    case EROFS: return G_EROFS;
    case EPIPE: return G_EPIPE;
    case EOVERFLOW: return G_EOVERFLOW;
    default: return G_EIO;
    }
}
}
