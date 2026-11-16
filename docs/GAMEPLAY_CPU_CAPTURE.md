# User-led Windows gameplay CPU sampling

This is a separate **external measurement**, not another optimization or game
build. Use the existing asset-free diagnostic EXE with its matching private
unstripped symbols retained for offline address attribution. Never replace the
installed EXE/profile. The agent does not run/control gameplay or start tracing.

Requires Windows WPR, Python 3.8+, and user approval to run the **capture helper**
as administrator. **Launch the game normally, not as administrator.** The helper
only queries process metadata and records CPU samples; it never launches, drives,
suspends, pauses, terminates, or injects into the game. No game-code change.

## Privacy and overhead

The custom `tools/gameplay_cpu.wprp` profile collects **system-wide** sampled CPU
stacks plus CPU/process/thread/module metadata, not just Dead Space. Paths and
other process names may appear. Keep every ETL/metadata/diagnostic file PRIVATE;
do not commit, publish, or automatically upload it. It does not request heap,
virtual-allocation, disk/file/network-content, input, screenshots or memory dumps.
Stopping/merging may additionally include normal machine/image metadata.

The memory-mode ring uses 64 MiB of configured ETW buffers, plus tracer/merge
overhead. This is NOT a memory-usage benchmark; avoid measuring RAM savings from
this run. Sampling perturbs execution. A busy system can overwrite the beginning
of the ring or lose events; inspect retained time/loss/stack completeness before
drawing conclusions. Default sampling rate is unchanged by this helper.

## One 60-second capture

1. Close unrelated game instances normally yourself. Launch the existing test EXE
   using its protected `--no-save --perf` launcher. For the first capture use the
   same **Legacy renderer**, 1280x720, 4x MSAA, requested VSync on, cap 60. Keep the
   chosen wait mode fixed; the prepared helper targets the existing `0bd1e68` EXE.
2. Load the same checkpoint and settle 30 seconds. Mark `/perf mark cpu_route`.
   Close the console. Do not turn on verbose/GL/input/geometry tracing or dumps.
3. Run the prepared CPU capture command as administrator in a SEPARATE console.
   It requires exactly one running `deadspace` process at the expected EXE path.
   Return to the game within 10 seconds and play a representative route/combat
   section normally, without renderer changes/minimization. Initial focus changes
   are expected while switching consoles; exclude that lead-in during analysis.
4. The helper stops its own recording after 60 seconds (merge can take longer).
   It does NOT stop the game. Afterward, quit the game normally yourself and keep
   its complete `deadspace.log` plus the new capture folder, particularly `cpu.etl`
   and `capture-state.json`. Tell us the approximate route and any unusual events.
   Send the file paths privately; do not post raw system-wide traces publicly.

Generic command (in an administrator console, after user launch):

```powershell
python tools/capture_gameplay_cpu.py capture --exe "D:\DSM-test\deadspace.exe" --output "D:\CPU-captures\new-run-01" --seconds 60
```

Every output directory must be NEW. No file/trace is overwritten. The conservative
preflight refuses an active default WPR recorder or unrecognized/localized status.
An independently named recorder may make start fail; the helper never cancels it.
Only its own UUID-named instance is stopped, always with `-instancename`.

Ctrl+C normally enters stop/merge cleanup. Do not close the console during a
capture. Power loss, forced console close, disk failure or WPR failure may leave
its named trace running. Recover explicitly as administrator:

```powershell
python tools/capture_gameplay_cpu.py finish --directory "D:\CPU-captures\new-run-01"
```

Recovery reads that helper's validated ownership metadata and targets only its
unique instance, never the default recorder. Even an ambiguous/failed start can
be recovered explicitly by its own name. Never run generic `wpr -cancel` or kill
the game to stop a trace. Existing `cpu.etl` is preserved; if stop reported failure
but produced a file, retain it and request status review rather than overwriting.

## Interpretation / validation limits

Use ETW process/thread identity and the recorded EXE hash to select the game.
Classify sampled instruction pointers in host code, driver modules, system calls
and anonymous executable/JIT regions. Host samples can be resolved offline with
the matching retained MinGW/DWARF binary and ASLR-adjusted addresses; Windows
PDB-only symbol resolution is not automatically sufficient. No symbols download
or analysis/upload is automatic. JIT unwind/guest-function naming may be incomplete;
an unknown address is not proof of a particular guest function or leak. Sampling
cannot directly measure GPU execution or validate gameplay behavior.

The WPR profile is checked using `wpr -profiles` and `-profiledetails` (read-only,
no trace). Unit tests use a fake backend only: bounded duration, no game control,
admin/process/status guards, fresh paths, unique ownership, interrupted recording,
failed start/stop and recovery, metadata-write failures, and no false success when
ETL is absent. Real ETW collection/loss/attribution awaits the USER's capture.
