# Main-menu Quit button

The bottom bar is ordered **Play, Options, Info, Extras, Quit**.
Quit reuses the game's bottom-button artwork, font, colour, baseline and case
policy, with the existing close/X icon centred in its icon well. Its caption is
left-aligned like the neighbouring buttons. The old top-right port button is no
longer inserted. No new game art or font is distributed.

On wide layouts the established slots remain in place, with Info and Quit swapped
from the first implementation. Where the middle gap is too narrow, all five
buttons form a compact row: only horizontal artwork
dimensions and positions shrink, not the font or button height. Rendering and
the port's Quit hit target use the same geometry.

Quit's artwork is a child of Extras' animated layout, inheriting that button's
existing fade phase, visibility and transform on entry, exit and submenu
transitions. It is not another `FadeUIAnimator` entry: that animator staggers its
list, so adding an entry changes the sequence and can make Quit appear early.
The prior registration hook is removed. There is no separate timer, copied alpha
or change to the original buttons' animation sequence.

Extras' copied wrapper also has independent OFF/ON child layouts. Sharing the
other wrapper's mutable captions/icons could make the third button display
Extras even though its native `BTN_INFO` action still opened Info. Only those
visual child references are separated; labels still use the game's localisation,
and button actions, placement, fonts and fade routing are unchanged.

A real left mouse click now reaches the same Quit handler as generated taps
from keyboard/controller navigation. That handler sends the same back key as
Escape, opening the existing X/checkmark exit confirmation rather than exiting
immediately. The click is consumed so it cannot also reach the confirmation as
a touch. Quit is exposed only when its artwork was added and all four original
main-menu buttons are usable; it is not active under dialogs or in submenus.

Asset-free tests cover placement, neighbour separation, wide-layout preservation,
narrow layouts, separate Info/Extras visual-child references, child/hit-target
agreement, icon centring, click boundaries and scaled coordinates. An offline
read-only asset probe also checks independent caption arrays in both visual
states, preserved native action IDs and the existing Extras-owned Quit child at
1280x720, 1920x1080 and 1024x768. The user verifies visual
appearance, mouse activation and confirmation/cancellation during playtesting.
The user confirmed the first build's appearance, and their private recording
identified the unsynchronised transition timing addressed by the follow-up.
