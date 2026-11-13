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
- The Mesa shader fix has not been implemented yet. Other reported terminals
  and the original asset set still need validation.

`.github/workflows/playtest-boundary.yml` fails if a PR from `playtest` targets
`main`. This is an additional warning, not a substitute for repository branch
protection or careful review. No such PR should be opened.
