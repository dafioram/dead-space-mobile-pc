// Only movement actually accepted from the port is ours to cancel at a handoff.
#pragma once

#include <cstdint>

namespace input {

class MovementHandoff {
public:
    void set_player(std::uint32_t player) {
        if (player == player_) return;
        player_ = player;
        moving_ = false;
        ++revision_;
    }

    void delivered(std::uint32_t player, bool moving, bool accepted) {
        if (!player || player != player_ || !accepted) return;
        moving_ = moving;
        ++revision_;
    }

    void native_movement(std::uint32_t player) {
        // A script/mobile scheme has replaced our intent; do not cancel its motion.
        delivered(player, false, true);
    }

    bool active(std::uint32_t player) const { return player && player == player_ && moving_; }

    template <class Stop>
    bool release(std::uint32_t player, bool native_accepts_release, Stop stop) {
        if (!active(player)) return false;
        moving_ = false;  // retire before calling the game: callbacks can re-enter
        const auto revision = ++revision_;
        if (!native_accepts_release) return false;  // native death/grapple/script now owns motion
        const bool accepted = stop();
        if (!accepted && revision_ == revision) moving_ = true;
        return accepted;
    }

private:
    std::uint32_t player_ = 0;
    std::uint64_t revision_ = 0;
    bool moving_ = false;
};

}  // namespace input
