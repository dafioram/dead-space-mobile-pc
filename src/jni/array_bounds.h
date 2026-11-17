#pragma once
#include "hle/io_buffer.h"

namespace jni {
inline bool array_range(u32 extent, s32 start, s32 count) {
    return start >= 0 && count >= 0 && u32(start) <= extent && u32(count) <= extent - u32(start);
}
// Check object-relative extents BEFORE guest mapping: an adjacent live allocation
// is mapped too, but it is not part of this array. No pointer is formed on failure.
inline bool array_span(u32 data, u32 extent, s32 start, s32 count, u32 element_bytes,
                       u32& address, u32& bytes) {
    if (!array_range(extent, start, count) || !element_bytes) return false;
    address = data;
    bytes = 0;
    if (!count) return true;
    const u64 begin = u64(data) + u64(u32(start)) * element_bytes;
    const u64 size = u64(u32(count)) * element_bytes;
    if (begin > UINT32_MAX || size > UINT32_MAX) return false;
    if (!hle::io_buffer(u32(begin), size)) return false;
    address = u32(begin);
    bytes = u32(size);
    return true;
}
}  // namespace jni
