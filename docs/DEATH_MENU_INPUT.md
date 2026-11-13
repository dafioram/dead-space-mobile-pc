# Death-screen input ownership

The port previously considered a recently updating player to be active gameplay
without checking whether that player was alive. Oxygen death can leave the player
updating through its death animation and restart screen without entering a pause
menu or one of the port's recognised bench/store dialogs. Captured mouse clicks
could consequently be consumed as gameplay events instead of becoming UI touches.
Retained private diagnostics show this mismatch: zero health and usable Yes/No
buttons while the port still reported gameplay capture. No private log is a test
fixture or part of the package.

The port now:

- Consults the game's native `GameObjectPlayable::isAlive()` before allowing
  gameplay capture, using its live result rather than a previous-update snapshot.
- Releases stale capture before dispatching input events, not only in the later
  per-frame input pass. Mouse input then reaches the existing touch/UI path.
- Explicitly shows the cursor when releasing to mouse-driven UI, while retaining
  controller cursor-hiding behaviour.
- Cancels pending fire and look deltas on release, discards queued gameplay events
  while dead, and clears the old player's queue on destruction. Diagnostic jobs
  remain available. A living respawn resumes normal input eligibility.

The same rule applies to other causes of death. Oxygen damage, the native death
sequence, button actions, UI layouts, saves and progression are not changed.
No forced Continue/Exit action or enlarged hit target is introduced.

Synthetic tests cover every combination of player freshness/liveness, pause,
cinematic and recognised-dialog state, plus focus, Left Alt and scripted capture.
They do not launch the guest or prove that both native death-screen actions
complete successfully. Live verification remains user-led: after suffocation,
check that the cursor is free without holding Alt, Continue reloads, and Exit
follows the game's intended path. Also check mouse look after continuing.

Installer/release automation is separate from this change. Existing CI uploads
development artifacts, not GitHub Release assets, and does not build the Windows
installer. No release is published by this input fix.
