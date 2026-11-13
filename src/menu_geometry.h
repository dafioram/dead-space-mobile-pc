// Main-menu bottom-bar geometry, in the game's layout units (480 x 320 minimum).
#pragma once

#include <algorithm>
#include <array>

namespace menu_nav {

struct MenuRect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct MainMenuBar {
    // Play, Options, Info, Extras, Quit; y is anchored 52 units above the bottom.
    std::array<float, 5> x;
    float scale;

    MenuRect quit_local_rect() const {
        // Quit is a child of Extras' animated layout, sharing its exact fade phase.
        return {x[4] - x[3] + 4 * scale, 4, 109 * scale, 31};
    }

    MenuRect quit_rect(float layout_height) const {
        auto r = quit_local_rect();
        r.x += x[3];
        r.y += layout_height - 52;
        return r;
    }

    MenuRect quit_icon_rect() const {
        // The existing 18 x 18 close icon, centred in the 29 x 31 icon well.
        return {5.5f * scale, 6.5f, 18 * scale, 18};
    }
};

inline MainMenuBar main_menu_bar(float layout_width) {
    // Keep the established positions when the middle gap can hold Info.
    if (layout_width >= 599)
        return {{{10, 118, (layout_width - 137) * 0.5f, layout_width - 255, layout_width - 146}}, 1};
    // At narrower aspect ratios that gap is too small. Fit five buttons as a row,
    // preserving their height and scaling only horizontal artwork/placement.
    float scale = std::min(1.0f, (layout_width - 20) / 565);
    float pitch = 113 * scale, left = (layout_width - 5 * pitch) * 0.5f;
    return {{{left, left + pitch, left + 2 * pitch, left + 3 * pitch, left + 4 * pitch}}, scale};
}

}  // namespace menu_nav
