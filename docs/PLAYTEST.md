# Playtest integration branch — never merge into main

`playtest` is a persistent integration destination for playing with all completed
fixes together. **Never merge this branch into `main`, and never open a release
PR from it.** Its integration-only files are not production changes.

## Branch workflow

- Implement and review each fix on its own themed branch, based on `main` or an
  explicitly documented dependency. Keep those branches available.
- Merge completed themed branches into `playtest` with merge commits. Build and
  playtest the combined result here.
- Submit the individual themed branches to `main` through their own PRs. Never
  use `playtest` as the source of a fix branch or as a shortcut into `main`.
- Bring newer `main` changes into `playtest` when needed, without rewriting shared
  branch history or discarding other work.
- Build artifacts must identify the exact integration commit and contain no APK,
  game assets, saves or texture dumps. Keep real-data playtesting local.

The initial baseline is `08f849e`, which already includes the asset guardrails,
asset-free test harness and Windows/Linux CI via their individual PRs. The Mesa
shader and terminal-interaction fixes are not part of this initial baseline.

## Integrated fixes after the initial baseline

- `fix/terminal-interactions` / PR #7: keep controls on large objects selectable
  when their bounds are nearby but their origin is distant. E activation of the
  reported gondola was verified locally with the fixed asset set and a disposable
  copy of the supplied save.
- `fix/interaction-transforms` / PR #8: resolve touch markers in world space,
  without reapplying the object's transform; use the player's full facing/up
  basis for targeting and its debug overlay. The reported side-panel position,
  a second control panel and an oxygen station accepted E in local testing on a
  disposable save. The user subsequently confirmed all terminals and the blocked
  door work, with a wall-oriented activation recorded in their local log.
  Follow-up `2988f2f` also rotates terminal zones with their marker axes, retaining
  metre dimensions and the corrected marker anchors. Drawing, radius checks and
  exact oriented-box selection share the same zones. A wall-oriented diagnostic
  pose, lower panel, oxygen station and gondola were checked locally; the rotated
  boxes still need the user's normal-play visual confirmation. Wall/ceiling and
  rotated-box geometry also have synthetic regression coverage.
- `fix/zero-g-slope-transitions` / PR #9: briefly use the game's existing
  sphere-sweep floor snap after grounded zero-G walking stalls. Keeps the supplied
  probe dimensions, collisions and orientation interpolation, and excludes jumping,
  airborne floor acquisition, death and ordinary-gravity movement. The bounded
  version crossed the first exterior exit on a disposable checkpoint with held W.
  The second exit passed an earlier continuous-sweep prototype but still needs
  user validation of the bounded version. Backing down toward the first airlock
  can still snag, as it also does with an equivalent pre-fix approach. See
  `docs/ZERO_G_TRAVERSAL.md` for scope and limits. Further gameplay testing is
  user-led; automated checks cover builds and the synthetic recovery policy.
- `fix/main-menu-quit` / PR #10: real mouse clicks reach the same Quit handler
  as keyboard/controller taps, opening the existing Escape/back confirmation.
  Quit moves from the top right into the bottom bar using the existing artwork
  and font. The user confirmed the first build's appearance. Follow-up `718839e`
  swaps Info and Quit, giving Play / Options / Info / Extras / Quit. Follow-up
  `84aaf17` replaces the separate staggered fade entry with artwork nested under
  Extras' animated layout, inheriting that button's exact fade phase without
  changing the native sequence. It also adds the existing close/X icon and
  standard left-aligned caption. Narrow layouts compact the row horizontally.
  Synthetic tests cover layout, child/hit geometry and icon placement; follow-up
  transition timing, icon appearance and confirmation are left to user playtesting.
- `fix/menu-widget-lifetime` / fork PR #11: starts from upstream PR #12, retaining
  jfgomez21's commit separately, and adds mapped-range/required-reference guards
  before rectangle queries plus cache retirement during supported button/bench
  destruction paths. Synthetic tests cover read ordering and invalid references.
  Neither the user nor the assistant has reproduced the reported workbench crash
  in this fork: this is defensive hardening, not a confirmed purchase/exit fix.
  Normal bench and menu/controller checks remain user-led; see
  `docs/MENU_WIDGET_SAFETY.md`.
- `fix/death-menu-input` / fork PR #12: consult native player liveness before
  gameplay capture, release stale capture before routing clicks, explicitly show
  the mouse cursor for UI and discard queued gameplay actions while dead. The
  retained suffocation diagnostic showed zero health and usable Yes/No buttons
  while gameplay capture remained active. Synthetic capture-policy tests pass;
  Continue/Exit behaviour and mouse look after Continue need user verification.
  No death animations, oxygen damage, native button actions or saves are changed.
- `ci/release-portable-assets` / fork PR #13: separate exact-tag portable-release
  automation and a manual installer builder. Public release assets come from
  main-ancestry tags, never a playtest-only commit. Existing `v1.1-rc1` was built
  at unchanged `aa79eb5` and given portable Windows/Linux assets plus a verified
  installer/checksums; it intentionally excludes later untagged fixes. See
  `docs/RELEASING.md`. The automation requires PR #13 to be merged into main.
- `fix/windows-release-staging` / fork PR #15: correct the Windows staging
  failure after a successful compile/test run. Tag resolution avoids command-
  wrapper-sensitive caret syntax, and Windows staging uses the host Python/Git
  context outside MSYS. Provenance is checked before compiling; real-Git tests
  cover lightweight/annotated tags and the Windows wrapper regression. This
  changes release tooling only and does not move RC2 or change its application.
- `fix/scripted-input-ownership` / fork PR #16: respect native HUD input routing,
  Brute intro/spawn freeze flags and the player's complete bench/store/suit-change
  animation sequence. Guard free-look/catch-up yaw with native facing permission;
  invalidate queued/pending port input with native scheme resets. Suppress touch
  fallback during player sequence locks without blocking object-owned menus.
  Windows/Linux builds and synthetic policies are checked; actual Brute reveal,
  store exits, suit changes and subsequent bench/control recovery need user
  validation. See `docs/SCRIPTED_INPUT_OWNERSHIP.md`. Zero-G inversion is not
  changed, and this is not a confirmed workbench crash fix.
- User feedback on `fix/scripted-input-ownership`: encounter camera locking
  provisionally passed. Store/suit-change transitions remain separate checks.
- `fix/zero-g-camera` / fork PR #17: rotate standing free-look around the body's
  current surface up, matching native body yaw instead of world Y. Share that
  rotation with interaction selection/debug geometry. Floor, ceiling, all four
  wall axes, tilted surfaces and yaw-transfer invariants have synthetic tests;
  actual zero-G visual checks remain user-led. See `docs/ZERO_G_CAMERA.md`.
- `diagnostics/zero-g-grapple`: opt-in `--trace-grapple` logs native transitions,
  end/cleanup, damage requests and sampled HP/state changes. Disabled by default;
  no damage suppression or forced detach. The report of continued damage after
  pushing a monster away is not yet independently reproduced or fixed. See
  `docs/ZERO_G_GRAPPLE_INVESTIGATION.md`.
- The Mesa shader fix remains unresolved (waiting for the upstream contributor).
  Other reported terminals
  and the original asset set still need validation.

`.github/workflows/playtest-boundary.yml` fails if a PR from `playtest` targets
`main`. This is an additional warning, not a substitute for repository branch
protection or careful review. No such PR should be opened.
