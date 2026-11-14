// Preconditions for querying a guest LayoutButton; mapped memory is not liveness.
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace menu_nav {

constexpr uint32_t kGuestPageSize = 4096;

template <class IsMapped>
bool readable_guest_range(uint32_t address, size_t size, IsMapped is_mapped) {
    if (address < 0x10000 || size == 0 || size - 1 > std::numeric_limits<uint32_t>::max() - address) return false;
    uint32_t first = address & ~(kGuestPageSize - 1);
    uint32_t last = (address + uint32_t(size - 1)) & ~(kGuestPageSize - 1);
    for (uint32_t page = first;; page += kGuestPageSize) {
        if (!is_mapped(page)) return false;
        if (page == last) return true;
    }
}

enum class ButtonQueryState { discard, inactive, ready };

// Memory supplies readable(address, size), read32(address), read8(address).
// Never query a rectangle until the widget and its required references pass.
template <class Memory>
ButtonQueryState button_query_state(uint32_t button, Memory& memory) {
    auto object = [&](uint32_t address, size_t size) {
        return memory.readable(address, size) && memory.readable(memory.read32(address), 4);
    };
    // Includes the enabled byte, sublayout, rectangle fields and active-region entity.
    if (!object(button, 0x5c)) return ButtonQueryState::discard;
    if (!memory.read8(button + 0x24)) return ButtonQueryState::inactive;
    uint32_t sub = memory.read32(button + 0x38);
    if (!object(sub, 0x55)) return ButtonQueryState::discard;
    if (!memory.read8(sub + 0x54)) return ButtonQueryState::inactive;
    // getActiveRect also invokes a virtual method on BTN_REGION (+0x30 slot).
    uint32_t region = memory.read32(button + 0x58);
    if (!object(region, 4) || !memory.readable(memory.read32(region), 0x34)) return ButtonQueryState::discard;
    return ButtonQueryState::ready;
}

}  // namespace menu_nav
