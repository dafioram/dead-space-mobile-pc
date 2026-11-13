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
  Quit moves from the top right into the bottom bar between Options and Extras,
  using the existing bottom-button artwork and font. Narrow layouts compact the
  row horizontally. Synthetic tests cover layout and click geometry; visual
  appearance and confirmation/cancellation are left to user playtesting.
- Death-screen selection and the Mesa shader fix remain unresolved. Other reported terminals
  and the original asset set still need validation.

`.github/workflows/playtest-boundary.yml` fails if a PR from `playtest` targets
`main`. This is an additional warning, not a substitute for repository branch
protection or careful review. No such PR should be opened.
