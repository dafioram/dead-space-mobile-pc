# Saving/loading reliability audit

Status: **implemented hardening, awaiting user gameplay/persistence testing**.
The historical audit below reviewed main `7598c4c`; the implementation updates
independent `fix/save-load-reliability` through main `2cc804e`. No save format,
native progression/slot rules, assets or release tags are changed. The agent
does not launch or drive gameplay.

## Implemented behavior

### Normal saving, atomic publication and backups

Native truncating writes to profile `.sb` files now use exclusively-created
sibling `.ds-save-*.tmp` files. The old final is not truncated. Save writes
complete partial progress, retry EINTR and fail on zero progress. Generic files,
pipes and non-truncating I/O retain normal POSIX behavior; direct non-truncating
profile writes are explicitly logged as **not** atomic replacements.

Publication requires complete writes, sync, successful close and bounded SBIN
inspection. Verified native filename-save hooks defer publication until the
serializer returns; false results discard staged output even if its bytes look
complete. Native FileStream close/flush results are corrected after imported I/O
failures, without skipping native cleanup. Unsupported hook layouts are logged;
checked atomic I/O remains active with validated-close publication, without
claiming native-result tracking for an unknown library.

On supported Windows/Linux filesystems, completed siblings replace the final
atomically, **without deleting it first**. POSIX uses rename and directory fsync;
Windows uses wide MoveFileExW replacement with WRITE_THROUGH and file `_commit`.
Actual power-loss guarantees still depend on the OS/filesystem/device.

Before overwriting a structurally validated known-format final, its previous
contents are copied to a separately-staged, synced and validated `<filename>.bak`.
The copy's identity must match the inspected previous file. Corrupt/unverified
finals never rotate over an existing known-good backup. There is one backup per
target. Backup/pre-publication failures retain the old final and report failure,
never silently fall back to truncation. An error **after** replacement is reported
as "new final installed; durability uncertain", not falsely described as rollback.

Handled failures remove only their own temporary. After process termination,
abandoned stages are logged and left alone, never silently promoted or deleted.
The port does not auto-restore backups, switch slots or repair data. Existing
native new-game/deletion/fallback decisions remain the game's policy.

### Load diagnosis and malformed input

`[save-load] selected` records the opened filename (including slot and Checkpoint/
Level role), byte size, FNV-1a-64 identity and validation classification/reason.
Inspection reads that same descriptor and restores its position, rather than
consuming the native stream or inspecting a newer replacement pathname. FNV
identities are diagnostic fingerprints, not cryptographic authenticity checks.

The separate bounded SBIN v2 inspector checks section boundaries/alignment,
duplicate/missing sections, every checksum, string lengths/counts, known schema
tables, object extents, root fields, array counts and known string/object
references. It does not reuse the unchecked asset-layout parser. Unknown versions,
types/kinds/enum/symbol extensions and inspection limits are **unverified**, not
assigned guessed sizes or blanket-banned. Inspection has a 64 MiB size cap and
a bounded semantic-work budget; structural validity is not gameplay compatibility.

For the verified FileInputStream layout, known structurally invalid selected
files return false **before native parsing**. The caller still owns/closes its
stream. Correlation uses that exact stream's descriptor, not the last open on a
thread; ordinary asset databases and unfamiliar formats are not blanket-blocked.

Native parse BEGIN/END logs distinguish serialization from later restoration,
assets and rendering. A parser success/checksum pass never claims a playable
checkpoint or reclassifies later scene crashes as corrupt saves. Startup records
source revision, persistent/disposable profile and library size/content identity.
No native error text is invented when the retail code exposes only false.

`[save-io]` failures include operation, path, real errno/reason and relevant byte
counts: open/read/write, zero progress, guest range/product overflow, seek,
truncate, flush/sync/close, validation, backup and replacement. Directory errors
are propagated. Missing/unused files are informational, not declared corruption.
Successful publication has an explicit **COMMITTED** line. Stdio error/EOF remain
distinct; clearerr clears the indicator, not a failed transaction's prohibition
on publishing partial data. Normal saves are tested repeatedly after failures.

### Ownership, Unicode and port preferences

An OS-backed `.deadspace-profile.lock` gives one cooperating instance exclusive
profile ownership **before** preferences load/write. A conflicting instance gets
an explicit startup error, not silently disabled saving. The OS releases ownership
on termination. The persistent lock filename is normal; do not delete it while
instances run. Older builds do not cooperate with this lock: close them before
using the same profile with a new build.

Same-path writers are excluded, including Windows case aliases. Wide filesystem
calls preserve UTF-8 paths on Windows. Guest normalization handles both slash
styles; atomic targets outside the selected root, symlink/reparse targets,
directories and explicitly read-only finals are refused with diagnostics.

`port_settings.ini` uses checked atomic replacement too, with a previous-readable-
text backup. Persistence failure is logged while runtime preferences remain usable.
`--no-save` still permits temporary progress/settings/backups inside its private
copy. Its lock and sidecars never go to the original; the private lock is released
before cleanup. Copying while another instance saves is still not a guaranteed
consistent multi-file snapshot; use a quiescent source.

## Validation and remaining limits

Three new asset-free suites exercise production I/O/inspection, actual descriptor/
stdio imports and native-result callbacks with simulated registers, and synthetic
helper processes. They cover repeated first/overwrite saves, exact backup bytes,
partial/EINTR/zero/mid-write disk-full failures, read/flush/sync/close/backup/replace
errors, serializer false, unclosed/deferred streams, invalid buffers/overflow,
stdio indicators, ordinary files/real pipes, read-only/invalid targets, Unicode,
writer/case contention, lock release after normal/killed helpers, and disposable
progress/preferences with original bytes unchanged.

Helpers terminate only themselves after staged writes, sync, close, backup and
final replacement. Finals remain complete; abandoned stages are never auto-loaded.
Validator tests cover every truncation, checksum/table/reference failures and
50,000 generated random/deep-checksummed mutations. Private supplied saves were
inspected read-only, never used as public fixtures or executed by tests.

Windows/Linux application builds pass all 14 suites. A clean application-off
Linux ASan/UBSan build passes all 13 asset-free suites. Native hook/layout sites
were checked read-only. No game session was launched, stopped or driven, and
no original profile/installation was written by agent validation.

This is **per-file**, not a whole-profile checkpoint/level/achievement journal.
Native multi-file ordering and semantic compatibility are not redesigned. It is
not proof of a fix for issue #22 or earlier render crashes, nor a destructive
migration or automatic recovery policy.

For user persistence testing, select a **copy** with `--data`, save/quit and
relaunch that same copy. `--no-save` intentionally discards progress across
launches. Check COMMITTED lines and the next selected filename/digest. If recovery
is needed, preserve the whole profile/log and close the game first: per-file
backups are not necessarily a mutually consistent checkpoint/level pair, so do
not blindly mix them.

## Historical audit findings (before implementation)

## What issue #22 does and does not establish

The tester reported incompatible saves and a chapter-2 load crash. The user's
Android-origin saves passed prior read-only checks and loaded chapter 2 locally.
Neither observation establishes why the tester failed. The retained chapter-2
screenshot reports an abort in the native M3G renderer; it does not demonstrate
that a save checksum failed or that writing the save caused the crash.

Treat these as distinct failure classes: missing/unreadable file, truncated or
corrupt serialization, unsupported schema/library/assets, valid serialization
with incompatible game state, and a later asset/render/runtime failure. Better
diagnostics should identify which stage failed, not label every load crash a
bad save. A passing checksum alone is not proof that a checkpoint is playable.

## Confirmed findings

### 1. Short writes can be reported as complete by the native stream

The inspected checkpoint path calls `im::serialization::Database::save` through
VFS output streams. `im::PosixFileSystem::openOutputStream` creates an
`EA::IO::FileStream` wrapped in `im::FileOutputStream`.

In this library, `EA::IO::FileStream::Write` checks whether imported `write`
returned an error, not whether it wrote the requested number of bytes.
`im::FileOutputStream::write` converts its boolean success into the original
requested byte count. In `src/hle/libc.cpp`, the host `write` adapter issues one
host write and returns its actual count. Consequently a short, nonnegative
write can look complete to serialization. This is a demonstrated code-path
gap, not evidence that a short write occurred in the tester's session.

Any write-all correction must be scoped to the appropriate regular-file/native
save stream contract, not blindly imposed on pipes, sockets, logging or all
POSIX writes. Retry interrupted operations, advance after partial progress,
and treat zero progress as failure rather than spinning. Even a correct
write-all loop cannot preserve an old save after a later write fails unless
the destination is also transactional.

### 2. Output is opened with truncation, without a port transaction

The ordinary native VFS output path selects `wb` and opens the destination with
create/truncate flags. `host_open_flags` translates guest O_TRUNC directly to
host O_TRUNC; `open` immediately opens that final mapped path. There is no
port-level staged save/atomic replacement or rolling known-good backup around
this path. A crash or storage failure after truncation can leave the previous
save lost even when the serialization code detects the later failure.

Checkpoint, level baseline, settings and achievement files are separate writes.
Per-file atomic replacement would improve reliability, but is not a guarantee
of transactional consistency across a whole profile or a checkpoint/level pair.
Their native ordering and selection rules must be understood before introducing
any multi-file journal or automatic recovery policy.

### 3. Durability and late failures are not reliably represented

- `src/hle/libc.cpp` registers `fsync` as unconditional success without syncing.
  The native `EA::IO::FileStream::Flush` also discards its return value; replacing
  the host stub alone therefore does not make native success reporting correct.
- The descriptor `close` adapter returns the host status without updating guest
  errno on failure. Native `FileStream::Close` discards that result and reports
  success, so end-to-end error reporting needs attention beyond the wrapper.
- Separately, `src/hle/stdio.cpp` calls host `fclose` but always returns success
  for a tracked stream. Its `fflush` returns the status but does not propagate
  host errno. These stdio issues are real, although the inspected checkpoint
  serialization path uses the descriptor-backed EA stream described above.
- The filename overload of native `Database::save` captures its serialization
  result before destroying/closing the output stream. A successful serializer
  return is not proof of successful close or durable publication.

Flushing userspace buffers, syncing a file, closing it, replacing a pathname and
persisting directory metadata are different steps. Power-loss guarantees also
depend on the filesystem/device; do not promise absolute durability.

### 4. Error classification is too weak

`stdio.cpp` reports every `fopen` failure as guest ENOENT, losing permission,
disk-full and other useful distinctions. Its directory-creation error is ignored.
Several descriptor/stat/directory operations in `libc.cpp` likewise omit or
generalize errno, and ordinary I/O failures lack a useful operation/path/count
diagnostic. Expected absence of an unused slot should not be logged like a
damaged selected checkpoint; inability to read a selected save should not look
like normal first-run initialization.

The native database loader already performs section-size/read and checksum
checks. Better load diagnosis should expose/augment those results, rather than
claiming checksums are entirely absent today.

### 5. Additional host-side opportunities

- `settings::save` rewrites/truncates `port_settings.ini` directly and checks no
  open/write/close result. This affects port preferences, not the SBIN campaign
  save, and should not be confused with the reported chapter-2 failure.
- Windows filesystem calls use narrow strings despite UTF-8 path sources such
  as SDL's base path. Test non-ASCII install/profile names and use appropriate
  Unicode path APIs at filesystem boundaries; spaces alone are not this issue.
- There is no profile-wide single-writer lock in the reviewed startup/I/O path.
  Two normal instances can target the same userdata. `--no-save` correctly uses
  a private copy, but copying while another process saves is not a guaranteed
  consistent multi-file snapshot.
- Startup logs identify paths/renderer, but do not unambiguously identify the
  source revision or the precise save selected. Add build, library and save
  identities so a later renderer failure can be related to the actual load.

## Proposed independent implementation slices

1. **I/O correctness and observability.** Proper error propagation, real syncing,
   the scoped complete-write contract, Unicode paths and operation-specific
   failures. Preserve general POSIX semantics outside the verified save path.
   Ensure a failed final commit cannot be presented as a successful save merely
   because the native serializer returned success.
2. **Bounded, read-only save inspection and load diagnostics.** Header/version,
   chunk lengths/alignment/checksums, string/schema bounds and reference checks;
   report the selected filename, size, digest, slot, map and checkpoint/level
   role, plus build/library identity. Mark stage boundaries: file read,
   serialization validation, state restoration, level resources and rendering.
   Distinguish structurally invalid data from unknown compatibility. Do not
   silently repair, delete or overwrite files, change slots, or load an older
   checkpoint without telling the user.
3. **Transactional publication and explicit recovery.** Write to a unique file
   in the destination directory, finish and sync it, validate its structure,
   then atomically replace the final file using verified platform semantics.
   Keep a bounded previous known-good backup without rotating corrupt input
   over it. Handle replacement/sharing/permission failure without first deleting
   the working save. Keep failed writes from publishing partial data and make
   recovery explicit rather than silently losing progress.
4. **Concurrency and preferences.** Per-profile single-writer ownership with
   separate disposable-session behavior; atomic `port_settings.ini` writes.
   Consider multi-file consistency only after the native save lifecycle is
   traced and tested, not by inventing a new save format.

The existing host `src/sbin.cpp` parser is primarily for asset layout/string
patches, **not** the game's save loader. It has unchecked internal lengths and
does not validate incoming chunk checksums; do not reuse it unchanged as an
untrusted-save validator. The private inspection script was an investigative
tool, not a production-grade malformed-input boundary.

The planned validator must use bounded reads/counts and explicit supported
versions, operate without executing the game, and avoid assuming all APK
variants have identical schemas. Valid but unfamiliar data needs an actionable
compatibility report, not guessed values or a destructive migration.

## Required tests before claiming improved reliability

Use generated synthetic files/fake I/O only in public tests. Inject partial
writes, EINTR, zero progress, disk-full/permission failures, failed sync/close,
failed replacement and termination between transaction stages. A failed save
must leave the previous valid final file intact, and no failure may be reported
as a successful publication. Test first save, overwrite, concurrent writers,
Unicode paths, backup retention and abandoned temporary files on Windows/Linux.

Load tests should include empty/truncated inputs, invalid sizes/counts, checksum
failure in each chunk, duplicate/missing required chunks, bad string/object
references, unsupported versions, and valid-but-unknown schemas. Verify that
diagnostics do not modify the input and that structural validity is never
misreported as full gameplay compatibility. Runtime checks need disposable
profiles and preserved original hashes, not committed saves or fixtures.

`--no-save` must continue to permit temporary checkpoint/settings changes while
protecting originals. Do not introduce an atomic-write bypass that publishes
outside its private root, or weaken its existing cleanup-error reporting.

At the time of the original audit, fault injection had not yet been run; the
implementation/test results above supersede that statement.
The tester's actual failing save, exact executable/library/assets and full
crash-time log are still needed to connect any hardening opportunity to #22.
