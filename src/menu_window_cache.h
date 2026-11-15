// Native draw snapshots for keyboard/controller navigation, not guest geometry calls.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>

namespace menu_nav {

struct WindowRef {
    uint32_t address = 0;
    uint64_t generation = 0;
    bool operator==(const WindowRef&) const = default;
};

struct WindowView {
    WindowRef owner;
    uint64_t frame = 0;
    std::array<float, 4> rect{};
    int32_t scroll_x = 0, scroll_y = 0;
    uint32_t panel = 0, vtable = 0;
};

class WindowCache {
    struct Entry {
        WindowView view;
        uint64_t observed = 0;
        bool drawn = false;
    };
    std::map<uint32_t, Entry> windows;
    uint64_t generation = 0;

public:
    WindowRef observe(uint32_t address, uint64_t frame) {
        if (!address) return {};
        auto [it, added] = windows.try_emplace(address);
        if (added) it->second.view.owner = {address, ++generation};
        it->second.observed = frame;
        return it->second.view.owner;
    }

    void retire(uint32_t address) { windows.erase(address); }

    bool capture(const WindowView& view) {
        auto it = windows.find(view.owner.address);
        if (it == windows.end() || it->second.view.owner != view.owner) return false;
        // Invalidate an older snapshot even when this draw's rectangle is invalid.
        it->second.drawn = false;
        for (float value : view.rect)
            if (!std::isfinite(value)) return false;
        if (view.rect[2] <= 0 || view.rect[3] <= 0 || !view.panel || !view.vtable) return false;
        it->second.view = view;
        it->second.drawn = true;
        return true;
    }

    const WindowView* find(WindowRef owner, uint64_t frame) const {
        auto it = windows.find(owner.address);
        if (it == windows.end() || !it->second.drawn || it->second.view.owner != owner) return nullptr;
        const auto& view = it->second.view;
        return frame >= view.frame && frame - view.frame <= 1 ? &view : nullptr;
    }

    void prune(uint64_t frame) {
        for (auto it = windows.begin(); it != windows.end();)
            it = frame >= it->second.observed && frame - it->second.observed > 2 ? windows.erase(it) : std::next(it);
    }
};

}  // namespace menu_nav
