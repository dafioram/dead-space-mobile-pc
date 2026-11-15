// The locator's native menu choreography is one request, not two queued inputs.
#pragma once

namespace input {

template <class Hud>
bool request_locator(Hud& hud) {
    const bool rig_was_open = hud.owns_rig();
    if (!rig_was_open) {
        if (!hud.owns_gameplay()) return false;
        hud.rig_button();
    }
    // Recheck the exact native RIG context, not ordinary gameplay ownership:
    // opening RIG intentionally revokes that ownership. Never target another UI.
    if (hud.owns_rig()) hud.marker_button();
    // Successful native marker activation closes RIG itself. If refused (busy,
    // disabled or no route), undo only the panel this request opened.
    if (!rig_was_open && hud.owns_rig()) hud.rig_button();
    return true;  // request handled; native code still decides whether it can start
}

}  // namespace input
