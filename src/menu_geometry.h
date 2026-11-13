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

    MenuRect quit_rect(float layout_height) const {
        // The off-state artwork's position and dimensions, also used for mouse hits.
        return {x[4] + 4 * scale, layout_height - 48, 109 * scale, 31};
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
