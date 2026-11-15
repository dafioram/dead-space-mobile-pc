# Saving/loading reliability audit

Status: **audit and proposed work**, not implemented save hardening. Reviewed on
2026-11-15 against production main `7598c4c` and the locally supplied Android
library. This branch starts independently from main. No save format, gameplay
code, original save, installation or release tag is changed by this document.

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

No fault-injection or bad-save reproduction has yet been run for this audit.
The tester's actual failing save, exact executable/library/assets and full
crash-time log are still needed to connect any hardening opportunity to #22.
