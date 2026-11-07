# Improvement plan after release fa904fe (2026-11-07)

State of the project after the fa904fe release, what the post-release survey
found, and the next work in order. Each item names its evidence, its gate and
what it does not establish. Entry point for the rest of the docs:
[README.md](README.md).

## 1. Where things stand

| Area | State | Evidence |
| --- | --- | --- |
| Release | fa904fe published with binaries for 6 platforms; hosted qualification 18/18 green | GitHub run 42145049881 |
| Correctness | 12/12 local CI lanes, Windows gate, CUDA tests + memcheck 0 errors, full TMJ-condyle-1 library (580.8M reads) byte-identical to e1da2d7 | data/validation/rc-fa904fe-20261105/ |
| Speed | **Unknown.** A/A noise 5% against a 2% gate; the WSL gate cannot see Windows host load | perf/aa-result.json, ab-result.json |
| Version string | Fixed in 1b9deb3: every build rereads the commit | docs/BUILD.md |
| GPU route | Correct, not faster. Mapping is 63% of a full-library run, so mapping-only acceleration caps at 2.7x against a 3x gate | docs/GPU_RESULTS.md 2026-11-06 |

### Whole-job budget, full library, 10 threads, CPU build

| Stage | Time | Share | Parallelism today |
| --- | ---: | ---: | --- |
| Setup (parameters, whitelist, genome) | 46 s | 2% | n/a |
| Mapping | 1797 s | 63% | 10 workers |
| STARsolo counting | 601 s | 21% | **1 thread** for the record-input loops |
| BAM coordinate sort | 392 s | 14% | 4 threads (`--outBAMsortingThreadN 4`), bins in parallel |
| Total | 2839 s | | |

### Code health

- No source file over 1,000 lines. Largest: `webui/WebUI_html.inc` 979, `webui/WebUI.cpp` 976.
- Two functions over 200 lines: `WebUI::run` (348 lines, 14 inline route lambdas) and `Parameters::registerParameters` (297 lines, a flat table of one line per option). Only `WebUI::run` is worth splitting; `StitchCore.h` (740 lines, header-only so CPU and CUDA share it) also stays.
- 88 TODO/FIXME in our own code, 3 of them `this can be parallelized` on the STARsolo input loops.
- Two `exit(1)` calls bypass the error path: `STAR.cpp:125` (unknown `--runMode`) and `webui/WebUI.cpp:974` (bind failure). `StitchCoreBridge.cpp:190,194` call `std::abort()` for internal bugs.
- 17 `catch (...)` sites; not reviewed for swallowed errors.

### Disk

| Location | Size | Notes |
| --- | ---: | --- |
| `build/` | 10 GB | 13 build directories; only `gpu-resident-cpu-control` and `gpu-resident-consolidation` are used by the gate |
| `.claude/worktrees/` | 5.5 GB | 3 worktrees, all merged into main, clean trees |
| `source/build/`, `source/build_icx/` | 0.8 GB | stray, gitignored |
| `data/local-ci/` | 0.7 GB | 27 runs |
| `data/validation/` | 90 GB | gitignored evidence. 27 GB of it is a second copy of the genome index (`SA` + `Genome`) inside `gpu-batched-seeding-20261101/profile/capture/`, same size and leading bytes as `data/genome_cynomolgus`; 16 GB is 42 BAM/SAM files over 100 MB whose hashes are already recorded |
| `D:\star-demo` | ? | user's, not touched |

## 2. Work items, in order

### P1. Housekeeping (½ h, no code change)

Remove the three merged worktrees and their `claude/*` branches, the stray
`source/build*` directories, the 11 unused build directories, and `data/local-ci`
runs older than the two fa904fe/1b9deb3 runs. Expected recovery about 15 GB.
Gate: `git worktree list` shows only main; the Windows gate still builds.

Evidence directories are a separate decision, not part of the sweep. Two
candidates, each only after the owner agrees: the 27 GB index copy under
`profile/capture/` (verify its hash against `data/genome_cynomolgus` first, and
point the seed-replay at the main index), and the 16 GB of raw BAM/SAM whose
hashes are recorded in their `result.json`/`artifact_hashes.json`. Keep every
hash, log, `Log.final.out`, SJ and Solo file; those are the revision-scoped
evidence CLAUDE.md says to preserve.

### P2. Last two raw exits (1 h)

Route `STAR.cpp:125` through `exitWithError(..., EXIT_CODE_PARAMETER, P)` and the
WebUI bind failure through the same helper with `EXIT_CODE_RUNTIME`, so every
failure leaves an `EXITING because of` line in `Log.out` and a documented code.
Add the bind-failure case to `scripts/test_webui.py` (occupy the port first).
Successful runs stay byte-identical.

### P3. Split `WebUI::run` (1 h, mechanical)

Move each route body into a named function in `webui/WebUI_routes.cpp`; `run`
keeps security setup, the route table and `listen`, about 40 lines.
Gate: `test_webui.py` on Windows and WSL, plus one manual job in the browser.

### P4. Parallel STARsolo counting (the one that moves the clock; 2–3 days)

Ten minutes of every full-library run use one of ten threads.

1. **Measure first.** Run a 50M-read prefix (Solo about 1 min) with the existing
   stage timestamps and split the 601 s between record input, UMI collapse,
   matrix writing and cell filtering for each of Gene, GeneFull_Ex50pAS and
   Velocyto. Keep `Log.out`; the full-library one was deleted with `~/star-rc`.
2. **Design.** The input loops read `P.runThreadN` independent temp files but
   write into shared per-CB cursors (`cbP[cb]`), `readFlagCounts` and
   `nReadPerCB*`, so threads over files would collide. Candidates: (a) partition
   CBs across threads, each thread scanning every file and keeping its CB
   range; (b) count-then-scatter with per-CB prefix offsets. Either must
   preserve the record order within a CB or show that `collapseUMIall` makes it
   irrelevant; STARsolo's duplicate and tie rules decide this. Pick after step 1
   shows where the time goes; `countCBgeneUMI`, `countVelocyto` and
   `readClusterUMIs` share the pattern.
3. **Validate** in order: unit tests, 5M contract (CPU and CUDA), then one
   full-library candidate run compared against the hashes already stored in
   `data/validation/rc-fa904fe-20261105/full-library/result.json` (BAM
   records, header, SJ, 33 Solo files). That needs the 29 GB FASTQs copied back
   from the NAS and one 47-minute run, not two.
4. **Gate:** all outputs identical; Solo wall time on the full library reported
   before/after from the same host. This is a stage time, not a qualified
   whole-job speedup (see P5).

Side note for the same pass: `--outBAMsortingThreadN` defaulted to 4 in the
qualification runs while 10 workers were available; document the setting or
reconsider the default. Changing it does not change output.

### P5. Honest speed measurement (½ day, then 30 min per trial)

`scripts/linux_paired_benchmark.py` reads `/proc/stat`, which in WSL2 omits the
Windows host. Add a host-load sample through WSL interop
(`powershell.exe Get-Counter '\Processor(_Total)\% Processor Time'`) alongside
the existing gate and record it in every result. Then rerun A/A then A/B of
fa904fe against e1da2d7 when the host is quiet (llama-server, pytest,
`pending_run`, `libgen_get` stopped). Until the A/A deviation is under 2% the
release makes no speed claim; after P4 the same harness qualifies the Solo gain.

### P6. Broaden the real-data oracle (1 day of machine time)

Correctness evidence is one 10x library with one Solo profile. Add one real
sample each for two-pass mapping, chimeric detection and long reads, run with
the e1da2d7 release binary and fa904fe, compared with the `full_library.py`
method. Records which modes the next release can claim as unchanged.

### P7. GPU route: decide the target before more kernels

With mapping at 63%, the 3x whole-job gate in
[GPU_FULL_PIPELINE_PLAN.md](GPU_FULL_PIPELINE_PLAN.md) is unreachable without
also shrinking Solo (P4) and the BAM sort. Options, for the owner to choose:
keep 3x and sequence P4 + a parallel sort first; or restate the gate as a
mapping-stage ratio plus whole-job non-regression. No further GPU kernel work
until that decision; the G0 timers in the plan are still the next GPU step.

## 3. Sequence and release

P1 → P2 → P3 can land this week as separate commits, each after the Windows
gate and the affected CI lanes. P4 is the next release's headline; it ships
only with the full-library identity result. P5 decides whether that release
states a speed number. P6 and P7 run alongside as machine time allows.

Not planned: splitting `Parameters::registerParameters` or `StitchCore.h`,
re-copying test data without a measurement to run, or any change to a
successful run's output.
