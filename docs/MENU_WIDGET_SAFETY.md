# Defensive menu-widget lifetime handling

This change starts with upstream
[PR #12](https://github.com/UndeadObvlivion/dead-space-mobile-pc/pull/12), commit
`add9f4a88615c412d3bf7605740a8cbbafae6c4b` by jfgomez21, retained as a separate
cherry-picked commit. Its useful change is checking disabled/detached buttons
before calling the guest rectangle method, rather than filtering them afterward.

The follow-up adds:

- Mapped-range checks before host reads, including page crossings and address
  overflow. A nonzero address alone is not sufficient.
- Rejection of null/unmapped sublayouts and active-region entities. The rectangle
  method calls through `BTN_REGION` as well as reading the widget's other fields.
- Cache retirement early in both `LayoutButton` destructor paths and the
  `BenchGridButton` destructor, before the guest releases their references.
  The bench destructor inlines its base cleanup, so only hooking the base would
  miss it. Cached keyboard focus is cleared too. Hooks use checked sites after
  PC-relative prologues, while `this` is intact and before fields are released;
  unmatched library addresses/instructions warn and leave those hooks disabled.
- Synthetic tests for ready, disabled, hidden, detached and unmapped states and
  read ordering. No actual guest objects or game assets are test fixtures.

The existing current/previous-frame update grace is retained. Disabled/hidden
widgets are skipped, not permanently removed; later updates can make them usable
again. Expired, invalid or destroyed entries are removed. Raw guest input,
workbench upgrades, purchase logic and save data are not modified.

## Validation limits

Neither the user nor the assistant has reproduced the reported workbench crash
in this fork. Treat this as defensive hardening, **not a confirmed fix** for all
of upstream issue #1. The PR author's reported reproduction concerns backing out
of an upgrade grid; the issue also describes exit after purchasing an upgrade.

Mapped memory is not proof of object liveness, and these guards do not validate
every descendant or helper-region vertex used by guest code. Destructor hooks
retire the paths that pass through those supported symbols; this is not a general
use-after-free detector or a fault-swallowing exception handler.

Live testing remains user-led: open a bench, select an item, enter/back out of
the node grid with Escape or controller B, purchase an upgrade if convenient,
then exit and re-enter. Also check ordinary menu and controller navigation.
There is no need to force a purchase solely to reproduce an unobserved bug.
