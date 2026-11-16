#pragma once
#include "memory.h"
namespace hle {
inline bool io_buffer(u32 address, uint64_t size) {
    if (!size) return true;
    if (!address || uint64_t(address) + size > (uint64_t(1) << 32)) return false;
    for (uint64_t p = address & ~uint64_t(mem::PAGE_SIZE - 1); p < uint64_t(address) + size; p += mem::PAGE_SIZE)
        if (!mem::is_mapped(u32(p))) return false;
    return true;
}
}
