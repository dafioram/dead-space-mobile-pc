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

**Current testing boundary:** all further interactive gameplay/playtesting is
reserved for the user. Agent validation is builds, asset-free/synthetic tests,
read-only inspection and packaging; do not launch, drive or stop game sessions.

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
- `feat/cli-help-no-save` / fork PR #18: early `--help`/`-h`, disposable
  `--no-save` userdata (saves and settings), and `docs/debug_menu.md`. Original
  userdata is copied, never linked or written by the session; native autosaves
  work on the temporary copy and relaunch restores the original starting save.
  This does not freeze checkpoints within a running session. Synthetic protection
  and early-help tests pass; actual gameplay remains user-led. The debug guide is
  allowlisted in portable packages, and this diagnostic build also lists
  `--trace-grapple` in help. Source snapshots/links and cleanup limits are
  documented in the guide.
- `fix/glsl-reserved-identifier` / fork PR #19: rename the AO shader's reserved
  GLSL local `packed` to `packed_distance`, without changing its math. The
  original compile failure was reproduced on AMD Windows OpenGL. All nine
  embedded shaders now compile and the scene/six post-processing programs link
  on AMD Windows and Linux Mesa llvmpipe; both application builds and eight
  test suites pass. Normal-play visual confirmation remains user-led. This is
  not a confirmed stretched-geometry or suit-upgrade fix. RC3 remains unchanged.
- The user confirmed the shader switch works with this fix. PR #19 is merged;
  production main through `96fca67` is integrated, including the merged PRs #16,
  #17, #18, #20, #21, #23 and #24.
- `feat/rig-credits-display` / fork PR #20: add a read-only credits tile left of
  the RIG node counter, reusing its styling and the existing credits icon. Read
  the store's native wallet without adding an item, changing money or touching
  on-disk assets. Synthetic layout tests and an offline private-copy layout check
  pass; live wallet, appearance and navigation confirmation remain user-led.
  See `docs/RIG_CREDITS.md`. RC3 remains unchanged.
- RIG credits follow-up `0a06124`: the user confirmed the balance but found the
  currency image covering the nodes. `IMAGE` draws at intrinsic texture size;
  the currency/background now use two-corner `STRETCH` bounds. The icon is 36x14
  layout units and left of the number, whose position is unchanged. Regression
  tests check the actual draw bounds and separation; revised appearance remains
  user-led and unconfirmed.
- RIG credits follow-up `46f6117` supersedes that placement: the user found the
  left icon too small/far away. It is now up to 56x28 units, six units right of
  the unchanged number, matching the node counter's ordering. Its width is
  limited to leave a gap before nodes; tests cover a narrower template too.
- The user confirmed that final currency layout is perfect on Windows only.
  Linux visual validation remains outstanding; currency geometry is unchanged.
- `fix/noninteractive-cinematic-input` / fork PR #21: treat fresh camera
  cinematics and native generic-animation state 29 as noninteractive, while
  explicitly exempting grapple routing/phases and swarms. Drop queued/fallback
  player input and filter native melee action events before their feedback
  audio; cancel a pre-existing mouse touch so it cannot span the cutscene.
  Preserve normal fighting, menu routing and existing cinematic skip, without
  stealing grapple input as a skip. Expanded synthetic policies and both builds
  pass; actual movie audio and grapple/QTE regression checks remain user-led.
  See `docs/NONINTERACTIVE_CINEMATICS.md`. No quit-crash fix is implied.
- Follow-up `8220c5f` extends PR #21 after the user could still trigger the
  sound during the boss encounter. Boss camera handoffs use native
  frozen-idle state 1 and return through `endIdleFrozen` to normal state 0;
  this was not covered by the initial movie/generic-animation check. Also cover
  known tram/collection/store/suit locks and full native freezes, but not partial
  freezes or arbitrary combat/camera states. Exempt native grapple/swarm states
  9/10, interactive shake-off state 12 and object-owned store/bench UI. Scripted
  locks do not gain cinematic skipping. Expanded synthetic coverage passes;
  boss sequence silence and interactive regressions still need user confirmation.
- Follow-up `9d68dd0` preserves object-owned UI despite frozen-idle/full-freeze
  body locks, not just store/bench menus. Native cinematics also request object
  input, so route 2 alone does not exempt a live movie or explicit tram/generic/
  collection animation. This protects interactive ownership without reopening
  movie clicks; synthetic tests cover both sides of that boundary.
- The user confirmed the extended scripted-input fix works. This does not imply
  exhaustive natural grapple/swarm/shake-off regression coverage.
- `fix/main-menu-info-label` / fork PR #23: the user found that the third button
  displayed Extras but still correctly opened Info. Separate Extras' OFF/ON
  visual child layouts from the regular wrapper's mutable captions/icons;
  preserve native localisation, action IDs, geometry and the Extras-owned Quit
  fade child. Windows/Linux builds and all nine suites pass; the private offline
  layout probe checks independent caption arrays and existing routing references
  at three resolutions. Live appearance/transition confirmation remains user-led.
- `fix/menu-movement-release` / fork PR #24 addresses issue #22 item 3: the
  ordinary queued stop could be discarded once RIG/object UI owned input,
  retaining the player's last delivered movement. Track accepted port movement
  separately and cancel it through the native zero-movement event before menu
  handoff, with independent reset/update cleanup. Keep menu ownership checks,
  native movement restrictions, grapple/swarm/shake-off motion and physical key/
  stick tracking. No player physics/freeze-field writes or save changes. Both
  application builds and nine synthetic test suites pass on the themed branch;
  the user now confirms the movement fix works, including the bench and a sewer
  door unlocked with a power node. Natural grapple/swarm/scripted-motion
  regressions remain outstanding; this is not exhaustive input validation.
  See `docs/MENU_MOVEMENT_HANDOFF.md`. Other issue #22 items remain separate.
- `fix/direct-locator` / fork PR #25 addresses issue #22 item 2: B/controller R3
  now request the native locator as one open-RIG/marker transaction instead of
  two queued events whose second was rejected by menu ownership. Keep native
  permissions, line/animation/feedback and ordinary queue/epoch gates; restore
  only a shortcut-opened RIG if activation is refused. The locator binding also
  works in an already-open RIG without toggling it away first. Windows/Linux
  builds and all nine synthetic suites pass on the themed branch; live locator
  display and menu/interactive regressions remain user-led and unconfirmed.
  See `docs/DIRECT_LOCATOR.md`; other issue #22 items are not closed by this fix.
- `fix/store-list-lifetime` / source `1dc3b61`: follow the user's reproduced
  store-entry crash from port menu navigation into stale window geometry.
  Replace the persistent item-window pointer and cached-window rectangle calls
  with per-owner generations and actual native draw/clip snapshots. Retire
  supported window/item destructor paths, including inlined store/bench cleanup,
  and keep item selection/scrolling within its own owner. Windows/Linux builds
  and nine suites pass on the themed branch. Bounded disposable Windows checks
  completed six entries with delivered movement before activation, store tab
  switching in Legacy/Native, two bench visits and Options control enumeration.
  No crash or invalid snapshot warning occurred. Developer positioning/level
  override was used; natural user reproduction, visual/controller and broader
  purchase/node-grid/scroll regressions remain unconfirmed. This is not proof
  that issue #22's reported softlock has the same cause or that the independent
  exit crash is fixed. See `docs/STORE_LIST_LIFETIME.md`. Production main through
  `7598c4c` (including merged PR #25) is now integrated; no iOS-import work is
  included and no release tag is changed.
- Store-entry follow-up: the user played combined `edfef88` and confirmed it
  was solid with no crashes. The reproduced store-entry crash is user-confirmed
  fixed; ChrisLynn94's original softlock and the independent exit crash remain
  separate. This confirmation does not cover every purchase/controller/grid
  regression scenario.
- `feat/debug-console-tools` / PR #27 / source `0526c7d`: resource commands set
  rather than add balances (`/credits`, `/nodes`, `/rich`, default 500,000/200).
  `/restore` uses native suit health/stasis maxima and requests native oxygen
  refill; `/enemies [radius]` lists nearby native enemies read-only. Strict
  parsing, game-thread application, player/world/input-epoch retirement and
  existing mutation ownership checks prevent invalid amounts and menu/script/
  grapple writes. Windows/Linux themed builds and all 10 suites pass. A private
  disposable Windows check verified setters/idempotence, malformed-input and
  open-RIG refusal with no replay, native health restoration to 200 and a nearby
  Slasher listing; original userdata hashes were unchanged. Oxygen depletion,
  other suits/modules and exhaustive actor/controller/sequence cases remain
  user-led. No enemy killing, saving-reliability or scene-cache change is
  included. See `docs/DEBUG_CONSOLE_TOOLS.md`. Main through `590b7d2`, including
  merged store PR #26, is integrated; production changes still use themed PRs.
- Debug-tool follow-up `150ed5c`: the user confirms `/restore` refills oxygen to
  full as desired. Other native eligibility states and suits/modules remain
  separate checks; this is not a save/lifetime crash confirmation.
- `fix/scene-cache-lifetime` / PR #28 / source `8765c7a`: native world/node
  generations, construction readiness and pre-destruction retirement for host
  scene/fade/fixture/fog observations, including address reuse and ParticleEffect.
  Custom node writes require live owner/node tokens, supported type/layout and
  current child-parent membership. Native transform queries balance temporary
  references without holding the registry lock across transforms/releases.
  Existing fade/light settings and all native animation/free/cleanup operations
  remain intact. Bounded read-only model/Object3D/IndexBuffer diagnostics retain
  evidence if crashes persist. Windows/Linux themed builds pass all 10 suites;
  a clean app-disabled Linux build passes 9 suites, and the production handler
  adapter passes ASan/UBSan. Hook sites were inspected read-only, not executed.
  Combined test registration retains both `scene_cache` and `console_debug`.
  No gameplay was launched or driven: the user owns chapter transitions, level
  jumps, normal exit and visual confirmation. The reported crashes are not yet
  confirmed fixed, and the dark/see-through store remains separate. See
  `docs/SCENE_CACHE_LIFETIME.md`. Save reliability is not included.
- `fix/save-load-reliability` / PR #29 / source `b24acf5`: checked per-file atomic
  profile saves, previous-validated-file backups, real sync/close/flush error
  reporting, native serializer-result gating, Unicode paths and writer locks.
  Port preferences are atomic too; `--no-save` still permits private temporary
  progress/settings without original writes. A bounded SBIN inspector and exact
  input-stream correlation reject known malformed selections before native parse;
  unfamiliar formats remain unverified, not blanket-banned. `[save-io]` logs
  operations/errors/COMMITTED status; `[save-load]` separates selected file and
  parse result from later restoration/assets/rendering. No format or native
  progression/slot-order changes, automatic repair/backup promotion, or whole-
  profile journal. Windows/Linux themed builds pass 14 suites; clean app-disabled
  Linux ASan/UBSan passes 13, including 50,000 synthetic mutations, actual bridge
  handler tests and helper termination at transaction boundaries. Eight retained
  private save copies pass read-only inspection, not gameplay execution. All
  interactive validation remains user-led. See `docs/SAVE_LOAD_RELIABILITY.md`.
  Production main through `2cc804e`, including merged PR #28, is integrated.
- User persistence confirmation on `6ce7726`: the next user-run log loaded
  `01_CheckpointSave.sb` at 3788 bytes / FNV64 `9923f1f71b4b35c6`, matching the
  previous run's final committed checkpoint, and native parsing succeeded before
  entering `02_mines_01`. The user confirmed their saved bench upgrades returned.
  This verifies that supplied checkpoint's persistence/restoration, not every
  save/schema/scene. The log subsequently reported a successful new checkpoint
  commit and normal shutdown. No agent gameplay was performed.
- `feat/geometry-diagnostics` / draft PR #30 / source `926a47a`: opt-in shared
  GLES observation before Native/Legacy diverge. `/geom on` arms; F4 marks;
  `/geom focus N` optionally outlines sampled candidate triangles; `/geom off`
  stops. F8 remains the renderer switch. Bounded position/index/attribute checks,
  local/eye/clip samples, state/caller fingerprints and recent-frame reports go
  into the private log without suppressing draws, altering geometry, writing
  saves or claiming mesh ownership. Shadow extents and mapped pages are checked,
  but guest allocation ownership remains unknown. Candidate ranking/through-wall
  x-ray overlays are not proof of corruption; no streak or armor-lighting fix is
  claimed. Windows/Linux builds pass 15 suites; app-disabled Linux ASan/UBSan
  passes 14, including 30,000 synthetic geometry mutations. Actual capture and
  highlight behavior awaits user testing. Instructions are explicitly allowlisted
  for portable staging, never captures. See `docs/GEOMETRY_DIAGNOSTICS.md`.
  Production main through `9ede69c` (merged save reliability PR #29) is integrated.
- Android-origin save compatibility: the user's four original save files use
  SBIN v2, pass all chunk checksums and match the comparison port save's common
  campaign/player/weapon/inventory/metadata/achievement field types. Android
  settings omit the comparison set's inactive slot-2/slot-3 activation flags;
  common types still match. Imported userdata files are byte-identical to those
  originals. The supplied --no-save log loads chapter 2 (`01_gov_02`) successfully,
  then uses a console level jump to the sewer (`03_water_01`) for menu tests and
  shuts down normally. No shader failure or crash is logged; missing font/beam-
  effect asset warnings remain separate. This confirms this Android save set,
  not all APK variants/original assets, natural chapter progression, or a fix
  for the other tester's chapter-2 crash. Save/log evidence is retained privately.
- The user completed the campaign and successfully started New Game+ in the
  same save slot. Completion also unlocks Hard for fresh games in other slots;
  this is separate from the completed slot's New Game+ restart. Item-by-item
  carry-over, later NG+ progression and relaunch persistence were not separately
  reported as checked.
- Exit crash remains open, separate from currency positioning: the user used
  Escape -> Quit -> Yes during gameplay. The retained log/dump are consistent
  with an invalid animation-track reference during native boss-scene teardown.
  The dump's PE identity matches `5ba6add`, not the later executable on disk.
  The underlying ownership failure is not diagnosed or fixed; no cleanup/error
  suppression is included in these UI revisions. Private evidence is not in Git.
- Other reported terminals and the original asset set still need validation.

- `feat/android-asset-support`, integrated into the combined playtest:
  original-layout APK validation/profile logging, staged no-replace extraction,
  identical-data reuse and early Reanimated/HD refusal. `--game-dir` selects
  imported assets/library; `--data` remains the independent save/settings root.
  `--import-only` exits before SDL/guest/profile/default-log/crash initialization
  and rejects gameplay/debug flags. The installer no longer deletes old game
  data before trying an APK. No physics, HD, renderer or audio fix is included.
  Main-based builds pass 16 Windows/Linux suites and 14 app-disabled Linux
  ASan/UBSan suites; combined integration passes 17/17/15, respectively.
  Synthetic coverage includes malformed ZIP/CRC, limits/path aliases, Unicode,
  SHA-256 vectors, write/close/publication failures and preservation of existing
  data. Installer source passes a compile-only check; no installer was run.
- The private user test build made before filename redaction is staged asset-free.
  Four fresh private cases A–D were prepared outside the repository, each with
  its own EXE, game data, future profile and user-run `Launch.cmd`. Import-only
  checks verify all 1,731 extracted assets / 328,192,023 bytes and each native
  library against its source APK; protected APK hashes still match the inventory.
  A/C/D report baseline ARM32; B reports the recognized CPU-detection variant,
  still awaiting independent gameplay/audio confirmation. An actual identical
  reimport preserves all output bytes/mtimes; actual Reanimated refusal creates
  neither parent nor game-data destination. No gameplay, profile initialization,
  installed-EXE replacement or original-save migration was performed. The user
  owns menu/texture/audio/transition/save/reload acceptance; import success is not
  a runtime-compatibility claim. No release/tag was created or moved, and geometry
  diagnostics remain playtest-only. See `docs/APK_IMPORT.md` for reusable commands.

- `feat/performance-diagnostics` / source `ea29913`: startup-only `--perf` adds
  bounded once-per-second process/guest-heap/cache/JIT/thread/audio telemetry and
  frame-loop work/swap/sleep/spin timing. `/perf status` and `/perf mark LABEL`
  provide read-only status/annotations. Level-load, world-lifetime and window
  events correlate repeated-load and minimized tests. No cache flushing,
  allocation strategy, native cleanup, frame pacing, physics, audio or gameplay
  behavior is changed. Graphics dimensions are explicitly RGBA-equivalent
  metadata, not measured VRAM; no GPU query, forced synchronization or dump is
  added. Main-based Windows/Linux builds pass 16 suites each; app-disabled Linux
  ASan/UBSan passes 14. Tests include the real allocator with synthetic allocations,
  event/frame limits, OS query lifetime/generations and production report adapters
  without SDL/GL initialization or guest execution. Combined user testing remains
  user-led; use a separate EXE folder and disposable profile, with matching
  diagnostic-on/off launchers. See `docs/PERFORMANCE_DIAGNOSTICS.md` for the test
  sequence and interpretation limits. This is measurement, not a leak/performance
  fix or release. Keep logs private and geometry capture off for the first baseline.

`.github/workflows/playtest-boundary.yml` fails if a PR from `playtest` targets
`main`. This is an additional warning, not a substitute for repository branch
protection or careful review. No such PR should be opened.
