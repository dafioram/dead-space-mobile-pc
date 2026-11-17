#pragma once
#include <cstdint>

namespace checked_size {
inline bool product(uint64_t count, uint64_t element, uint32_t limit, uint32_t& bytes) {
    if (element && count > limit / element) return false;
    bytes = uint32_t(count * element);
    return true;
}
inline bool round_up(uint64_t size, uint32_t alignment, uint32_t limit, uint32_t& rounded) {
    if (!alignment || (alignment & (alignment - 1)) || size > limit) return false;
    uint64_t value = (size + alignment - 1) & ~uint64_t(alignment - 1);
    if (value > limit) return false;
    rounded = uint32_t(value);
    return true;
}
}  // namespace checked_size
