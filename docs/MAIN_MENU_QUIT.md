# Main-menu Quit button

The port's Quit button belongs between Options and Extras in the bottom bar.
It reuses the game's bottom-button artwork, font, colour, baseline and case
policy. Its caption is centred and the old top-right port button is no longer
inserted. No new game art or font is distributed.

On wide layouts the original four buttons remain in place. Where the middle
gap is too narrow, all five buttons form a compact row: only horizontal artwork
dimensions and positions shrink, not the font or button height. Rendering and
the port's Quit hit target use the same geometry.

A real left mouse click now reaches the same Quit handler as generated taps
from keyboard/controller navigation. That handler sends the same back key as
Escape, opening the existing X/checkmark exit confirmation rather than exiting
immediately. The click is consumed so it cannot also reach the confirmation as
a touch. Quit is exposed only when its artwork was added and all four original
main-menu buttons are usable; it is not active under dialogs or in submenus.

Asset-free tests cover placement, neighbour separation, wide-layout preservation,
narrow layouts, click boundaries and scaled coordinates. The user verifies visual
appearance, mouse activation and confirmation/cancellation during playtesting.
