# Store/bench list navigation lifetime

## Scope and evidence

A user reproduced a store-entry crash in playtest `8c51c98` after several visits
to a store. The private log records successful native movement cancellation and
object input handoff, then an invalid guest read in
`ScrollingWindow::getWindowRect`. Return-address evidence from the matching
executable identifies the port's `menu_nav::item_view` / `all_buttons` /
`menu_nav::event` path. The small dump does not establish the exact point where
the window or a descendant entity became invalid.

This change removes that unsafe extra geometry query and corrects the ownership
model. It is not confirmation that issue #22's blank-store softlock has the same
cause, a fix for every workbench crash, or a fix for animation-track teardown on
game exit. No private log, dump, executable, save or screenshot is a test fixture
or part of the source/release package.

## Changes

- Remove the single persistent `g_item_window`. A store or bench item belongs to
  the window whose native draw is currently drawing it, not the window last seen
  handling a panel event. The first draw can precede the first panel event.
- Track a generation for each observed window lifetime. Destruction immediately
  retires its viewport, associated button/item observations and keyboard focus.
  Reuse of the same mapped address gets a different generation.
- Observe both ScrollingWindow destructor bodies and the store/bench destructor
  paths that inline their base cleanup. Retire individual StoreItem/BenchItem
  observations before their fields are released, including list/tab changes.
- Snapshot the real rectangle and scroll values during `ScrollingWindow::draw`,
  after its native rectangle calculation/alignment and before `setClip` and the
  panel draw. A balanced draw-return scope establishes ownership for nested
  windows and item draws.
- Both list clipping and button-to-window offsets use these host-side snapshots.
  Menu navigation no longer calls `getWindowRect` through a cached window.
  Native rendering still performs its original calculation, unchanged. There is
  no exception swallowing, invented fallback rectangle or hidden render failure.
- Geometry must come from a current/previous-frame native draw; an item must
  match its owning draw's frame and generation. Not-yet-drawn or expired views
  are not offered as unclipped keyboard/controller targets. Ordinary native
  touch handling remains available.
- Item spacing, next-item selection and scrolling updates stay within the same
  owner. Scrolling retains the existing native window scroll fields, requiring
  a tracked fresh draw plus matching vtable/panel and readable window range
  before writes. This is UI scrolling, not player/physics manipulation.

All new hook sites check the expected library offsets and two displaced
instructions. Mid-function destructor sites are after PC-relative setup and
before reference cleanup; their displaced instructions do not address the PC.
If the supported sites cannot be installed, warn and do not enable window-draw
snapshots/scrolling navigation for that library. Do not fall back to unsafe guest
geometry calls. This is not support for arbitrary game-library variants.

## Validation

Asset-free core tests cover event-before-draw and draw-before-event ordering,
independent simultaneous window snapshots, current/previous-frame freshness,
retirement during the same frame, address reuse, stale late captures, repeated
entry/exit, expired owners, native signed scroll values and invalid/missing
geometry. All inputs are synthetic; these tests do not execute a retail library,
render menus or prove the user's crash is gone.

Windows and Linux application builds and all nine CTest suites pass on the
themed branch. Native hook sites were inspected against the locally supplied
Android library. Gameplay and visual confirmation remain user-led.

A bounded Windows smoke test used `--no-save`, the supplied Android-origin
profile, a developer level override and developer positioning near the chapter-4
store. Six entries included directional movement delivered before interaction,
with successful movement handoffs recorded in the log; the store opened each
time. This covered forward/backward/strafe, Legacy and Native renderer modes,
plus store inventory/shop tab switching. Two bench visits exposed the horizontal
item list, and a separate Options check enumerated its navigation controls.
No crash or invalid-window-snapshot warning occurred. These are automated smoke
checks, not a natural route, an old/new deterministic crash comparison, a visual
sign-off, or exhaustive controller, scroll, purchase or node-grid testing. All
captures and logs remain private outside the checkout.

## Playtest checklist

Use the combined `playtest` executable, with `--no-save --trace-input` and the
intended `--data` root. Do not overwrite assets or original profiles.

1. Revisit the store from the reported chapter-4 location. Enter/exit repeatedly,
   including holding/releasing forward, backward and strafe keys across entry.
   Repeat while stationary and with mouse/controller input. Check the first
   directional key after the shop opens.
2. Navigate store rows and tabs, scroll offscreen entries into view, use native
   mouse/touch selection, open/dismiss confirmations, close, and immediately
   re-enter. No purchase is required solely for this test.
3. Open a bench, navigate the horizontal item list, enter/back out of its node
   grid, close and revisit. Confirm focus is sensible and scrolling is confined
   to the current list.
4. Check Options page turning, vertical scrolling, mouse controls and keyboard/
   controller highlights. Both wide-screen and smaller window layouts matter
   because navigation now uses the actual native draw clip.
5. Confirm ordinary walking, RIG, locator, menus and scripted sequences remain
   unchanged. Test Native/Legacy if practical.

Startup should log `menu navigation: native window draw snapshots and list
lifetimes tracked`. Unsupported-site or invalid-native-snapshot warnings should
be investigated, not dismissed as a successful fix. Retain any new failure's
exact build identity, full log and dump privately. Until this checklist is
confirmed, describe the change as a targeted lifetime fix/hardening, not a
gameplay-confirmed resolution of issue #22.
