# Qualification execution record — 2026-10-07

This is partial execution of [QUALIFICATION_PLAN.md](QUALIFICATION_PLAN.md), not
five-platform promotion, a performance claim, or completion of ownership work.
C++17 remains the release default; CUDA remains optional and off by default.
Local receipts are under `data/validation/qualification-20261007/`; they are
ignored and may contain private runtime paths. They are not release assets.

## Implemented

- Release invokes the reusable qualification workflow at its own SHA and requires
  it, native builds and s390x before publication. Qualification artifacts use a
  separate prefix and cannot be collected as release binaries. Bypass-negative
  controls cover removal of qualification, external workflow substitution and
  publication with `always()`.
- Windows and both macOS architectures now execute miniature analysis and
  WebUI checks in C++17/20 lanes. Root CTest includes dependency tests. C++20 lanes
  compare same-source C++17 and restore the original configuration afterward.
- s390x asserts actual architecture/byte order, installs Python, enables bounded
  endian/packed tests and mapping/count checks in C++17/20. It no longer ignores
  probe failure. Hosted/QEMU execution has not been performed in this turn.
- The baseline is the hash-pinned released binary, not a rebuild with moving
  dependencies. Both CUDA targets honor independently validated
  `STAR_CUDA_STANDARD=17|20`; a global CMake override is no longer misleading.
- Benchmarking supports paired balanced-randomized rounds, excluded warmups,
  fixed seed, Windows OS peak working set/commit and GNU-time peak RSS. A/A
  calibration uses median **absolute per-pair** relative deviation, so opposite
  timing errors cannot cancel into a false pass.
- Full-contract checks compare exact BAM record multisets using bounded SQL
  cache, scientific headers/statistics, raw/filtered/EM matrices and axes/SJ.
  Floating matrices are exact by default; no tolerance is inferred post hoc.
  Full-contract reference reuse is rejected until independently qualified.
- Immutable source archives, source/binary hashes, local compiler/cache/command
  records and OS/GPU receipts can be captured with `capture_qualification.py`.
- PackedArray allocation base and mutable view are separated; snapshot copies
  borrow rather than copy release authority. BAM buffers/streams have unique
  owners, borrowed BGZF is never closed by their destructor, and deferred write,
  flush and close failures block successful completion. See
  [OWNERSHIP_AUDIT.md](OWNERSHIP_AUDIT.md) for exact limits.

## Executed local evidence

| Check | Observed result |
| --- | --- |
| Python gate tests | 17 tests: Windows 13 executed/4 POSIX-only skips; WSL 16 executed/1 Windows-only skip |
| GCC 13.3 C++17, MSVC 19.51 C++20 | 95 root CTest cases passed; miniature analysis and WebUI passed |
| GCC same-source C++17/20 | Language-pair comparison passed, source signatures retained |
| GCC ASan/UBSan | 95 root tests and miniature analysis passed; deferred `/dev/full` BAM failure rejects completion |
| Windows host20/device17, CUDA13.3/MSVC19.51 | 95 tests, actual remap/seed integration and missing-device rejection passed; no unsupported-compiler override in final build |
| Windows host20/device20, CUDA13.3/MSVC19.44 | Final incremental build: 95 tests and actual remap/seed integration passed |
| WSL host20/device20, CUDA13.0/GCC13.3 | 95 tests and actual remap/seed integration passed |
| CUDA memcheck | Windows and WSL remap tests: zero reported errors; not whole-application race/leak proof |
| Real 100K paired-prefix correctness | Before/after exact BAM/header, raw/filtered/EM/velocity matrices, axes, SJ and scientific statistics passed; peaks about 31.5 GB |
| Real 100K CPU vs CUDA required backend | Full scientific contract passed; real `GPU_SJDB_REMAP status=0`, 24,172,833,228 device bytes and 180 chunks recorded; no speed conclusion |
| Real-data repeated performance | Five measured A/A pairs plus one excluded warmup pair: exact scientific outputs; median absolute timing deviation 4.02%, over the declared 2% threshold; `INCONCLUSIVE` |

The real-data prefix is not a random whole-library sample. The initial 90.24s vs
60.01s runs had cache/order and concurrent-build confounding; they are correctness
screening, not a measured optimization. Another task occupied about 66 GB of
GPU memory during this turn; no other task was stopped.

The preserved calibration run loaded an earlier assessment implementation.
`calibration-100k-adjudication.json` binds its immutable measurement receipt to
the corrected per-pair absolute-deviation calculation and is authoritative for
the noise decision. The measurement receipt was not overwritten. Future reuse
recomputes calibration and checks input/binary/backend and full-artifact hashes,
rather than trusting its printed status. No ten-pair A/B acceptance run was started
after this failed noise gate.

The real CUDA correctness screening recorded 75.52s CPU vs 57.46s CUDA and host
peaks 31.48 vs 31.60 GB. This was another single cache/order-confounded pair, not
evidence that CUDA became faster. Its upload/kernel/download times were
3.420/0.064/3.590s. Scientific equality and actual backend execution are the
qualified results; end-to-end acceleration is not.

## Continuation: bounded IPC ownership

- `SharedMemory` cannot copy or implicitly move IPC release authority. Counter
  mappings now detach, explicit cleanup is idempotent, mapping size is obtained
  from the OS, overflow and allocator-race identity are checked, and failed or
  invalidated usage counters no longer authorize automatic segment removal.
- Linux root CTest: **97/97 passed**, including separate SysV/POSIX two-process
  lifecycle fixtures. Both IPC fixtures also passed **20 repetitions each** in
  parallel under ASan/UBSan with leak detection enabled.
- Real miniature STAR SysV `LoadAndExit`, `LoadAndKeep`, `LoadAndRemove` and
  explicit `Remove` passed; BAM records, SJ output and scientific final-log
  fields exactly match NoSharedMemory. Existing BAM/count/Velocity/error tests
  remain passing. Linux CI now requests this real shared-memory coverage.
- Windows MSVC C++20 rebuilt and its miniature NoSharedMemory integration passed;
  no Windows IPC qualification is claimed. Hosted macOS/SysV/POSIX execution is
  still pending.
- A fault-injection assertion initially exposed Linux's successful `IPC_STAT`
  on an `IPC_RMID`-marked counter; the revised policy checks `SHM_DEST` where
  available. Parallel tests also exposed key-file inode reuse; the fixture now
  retains the inode and skips occupied keys. No blanket IPC cleanup is used.
- This is part of O3, not completion of Genome heap/shared ownership, concurrent
  admission safety, full CLI leak detection or the performance gate. The earlier
  4.02% noise calibration remains inconclusive; no new speedup is claimed.

## Remaining mandatory gates

### C++17 ownership continuation (base commit `2e34721`)

- Genome sequence allocations now have a unique allocation-base owner distinct
  from G/G1 views. Heap generation/load/transformation/output paths adopt their
  buffers; all suffix-array owners/views clear on explicit free. Shared mappings
  have a separate owner, released before Parameters' log streams. Error handling
  no longer unconditionally removes shared data after a failed attachment.
- All three insertion-time Genome snapshots require `Snapshot::Borrowed`;
  implicit copies and assignment are disabled. Snapshot pointers/scalars are
  initialized, and snapshots do not duplicate heap or IPC release authority.
- ReadAlign's large read/window/transcript/BAM arenas have named unique bases;
  existing mutable algorithm views, sizes and initialization are preserved.
  Chunks own primary/WASP/merged aligners, input/output streams, junction outputs
  and local quantifications. Worker resources release after join and all Solo/BAM
  consumers; pass1 resources release before pass2 index insertion.
- Per-read Solo data/streams and CR4 scorers have owners; early barcode release
  clears its view. SpliceGraph owns its actual allocated rows/columns/seeds,
  replacing a destructor that iterated to 100000 rather than the row count.
  The stream-open retry reuses its stream instead of leaking the failed attempt.
- **C++17 only in this continuation:** GCC Release and GCC ASan/UBSan root CTest
  **99/99**, native MSVC Release **97/97**. Unit leak detection remains on.
  Miniature normal/two-pass/WASP/merged-PE/chimeric, sorted/transcriptome, count,
  Velocity and rejection fixtures pass; scientific outputs match references.
  Full-CLI sanitizer integration retains its declared leak-detection exception.
- Windows was verified from a fresh UTF-8-console build; Ninja records the
  actual Genome/ReadAlign/SpliceGraph/SoloReadFeature header dependencies. An old
  localized cache missed header changes and was not used for final qualification.
  WSL build/log directories are persistent rather than `/tmp`.
- Real **100000-pair** Windows screening: exact full scientific contract passes
  against the preserved candidate (BAM records/header, SJ, scientific log fields,
  all Solo matrices/axes including filtered/EM). Single runs took 47.39s vs 41.83s;
  cache/order and reference C++20 versus candidate C++17 confound performance.
  This is correctness/capacity screening, not speedup or time/RAM acceptance.
- A separate full-CLI LeakSanitizer diagnostic (same persistent miniature index,
  two threads, no SAM output) reports **208438266 bytes / 20772 allocations**
  for the preserved pre-migration sanitizer binary versus **48086 bytes / 660
  allocations** for the candidate. Both diagnostic processes exit nonzero for
  remaining leaks; this is not a leak-gate pass. No ReadAlign/ReadAlignChunk
  constructor frame remains in the candidate leak report. Remaining reports
  include Genome metadata and parameter/helper allocations. This observation is
  specific to the stated fixture and is not a general peak-RAM reduction claim.
  The real-prefix peak RSS changed from 31480541184 to 31480922112 bytes (+380928);
  only replicated calibrated measurements can establish the time/RAM budget.

### Remaining ownership boundaries

Genome metadata arrays, optional output-genome/variation/super-transcriptome
aggregate owners and process-global/Solo aggregates are not fully migrated.
Borrowed snapshots do not extend source lifetime. Full CLI leak freedom,
allocator-failure injection for every arena, long-read and super-transcriptome
execution, concurrent shared-memory admissions and fatal-exit unwinding are
not certified. New unit ownership tests do not replace those remaining gates.

- Execute hosted GCC/Clang, Windows, both macOS architectures and s390x on a
  committed/pushed immutable SHA; the workflow has not been dispatched here.
- Establish fully pinned toolchain/container lanes, non-AVX2 execution and
  Multi-Config Debug/Release qualification. Current records do not prove them.
- Finish noise calibration before ten-pair A/B performance acceptance; use an
  uncontended machine. Whole-library random sampling, 1M/5M/full-library scaling
  and resident-GPU break-even measurements remain unqualified.
- Complete the remaining O3/O4/O5 scope with metadata/aggregate ownership,
  long-read/super-transcriptome/cancellation/fatal-exit, allocation-failure and
  checked GPU finalization fixtures; pass2 and standard per-thread teardown now
  have miniature integration coverage.
  Scoped buffer ownership is not complete CLI leak freedom.

This ownership continuation is uncommitted. No C++20-default promotion or release
publication was performed.

## Metadata and experimental-path continuation (C++17)

This continuation supersedes the earlier *remaining Genome metadata* entry, not
the remaining whole-process, fatal-exit or scientific-qualification boundaries.
The parameter registry now shares registration lifetime across the existing
pass1 copies; registered field addresses still borrow the original Parameters.
The genome-parameter reader borrows existing streams instead of discarding a
fresh stream allocation. Chromosome bins, SA start tables and junction tables
have explicit owners; older tables stay alive until the source Genome is
destroyed because insertion snapshots can still borrow them. Array capacities
and uninitialized allocation semantics are unchanged. Variation/SNP/VCF/sort
scratch, optional output Genome and SuperTranscriptome owners are scoped.
The insertion reader and seven SJ preparation scratch arrays are also scoped.

### Verified evidence

- Final frozen-source Windows C++17 Release: **98/98 CTest PASS**; Linux C++17
  Release and Linux ASan/UBSan Debug: **100/100 PASS**. Sanitized unit tests keep
  leak detection enabled. Python harnesses: 17 total, 13 executed/4 skipped on
  Windows, 16 executed/1 skipped on Linux; all executed tests pass.
- Final miniature before/after regressions pass on Windows, Linux Release and
  Linux ASan/UBSan: two-pass, merged/chimeric, WASP, ordinary/sorted/transcriptome
  BAM, scientific logs, junctions, Gene/GeneFull and all three Velocity layers.
  Full CLI integration still disables leak detection for unconverted aggregate
  and fatal-exit paths; this is distinct from the dedicated leak checks below.
- Identical two-thread ordinary CLI leak fixture: 48086 bytes/660 allocations
  before this continuation, then 76 bytes/9 allocations, then **exit 0 with leak
  detection enabled and no leak report** after table ownership. WASP likewise
  passes the dedicated leak check. An additional two-pass check exposed 8814
  bytes/9 allocations in an input stream and SJ scratch; after their scoped
  cleanup it too exits 0 with leak detection enabled. This is not universal CLI
  leak freedom, peak-RAM reduction, or fatal-exit cleanup certification.
- `test_experimental_genomes.py` builds Full and SuperTranscriptome indices and
  executes real exonic/spliced graph diagnostics. Index bytes and graph scores
  match the reference on Windows and Linux. Final STARlong C++17 executes actual
  1200/1600-base reads, both uniquely mapped, including a splice. This checks
  execution and teardown; no independent previous STARlong oracle was supplied,
  so it does not certify general long-read accuracy/equality or performance.
- **Experimental-output limitation discovered:** upstream SpliceGraph executes
  DP but its alignment-output conversion/statistics are unfinished. A successful
  request produced an empty BAM; SAM conversion also has an empty-iteration
  path. We did not invent mapping results or certify that output. Loaded
  SuperTranscriptome BAM/CRAM requests now fail with an actionable explanation;
  `--outSAMtype None` remains diagnostic-only. Full genome output is unchanged.
- Final native C++17 binary `431c13e7d52ccfa9a187feecc60be464dc6e909312c2d064c6b75e3aa5da050e`
  passes the **100000 synchronized-pair full scientific contract** against the
  preserved pre-ownership native binary: exact BAM multiset/scientific header,
  SJ, declared raw/filtered/EM Solo matrices/axes (including Velocity) and
  scientific final fields. Receipt:
  `data/validation/metadata-final-100k-20261007/result.json`.
  Before: 45.7741 s / 31481479168 peak RSS bytes; after: 47.0758 s /
  31460265984 bytes. One run each, prefix sampling, cache/order contention and
  C++20-reference/C++17-candidate differences prevent time/RAM acceptance.
  The earlier noise calibration remains INCONCLUSIVE; no speedup is claimed.

All local continuation receipts use the `metadata-` prefix under
`data/validation/qualification-20261007/`. Final small-regression logs use
`metadata-final-frozen-*`; final long-read executable/configuration/commands
use `metadata-final-long-*` and `STARlong-metadata-final-cpu17`. These ignored
artifacts are local audit evidence, not release assets. Failed exploratory
attempts are retained: the first graph test correctly rejected the empty BAM;
an incremental Release build overlapping a header-layout edit produced mixed
objects. A frozen-source header-forced rebuild and full miniature regression
passed. Never build/test changing ownership layouts concurrently with edits.

### Still unverified

Complete Transcriptome/Solo aggregate ownership, all genome-generation helper
streams, allocation-failure injection, cancellation/fatal-exit unwinding,
concurrent shared-memory admission, hosted cross-platform qualification and
noise-calibrated performance/scaling remain separate work. Experimental graph
alignment output requires an explicit algorithm/output-contract repair and
independent oracle, not a memory-cleanup patch. No C++20 promotion, commit,
push or release publication was performed in this continuation.

## Transcriptome / Solo aggregates (next local continuation)

The preceding ownership batch was committed and pushed as `b4b1eec`.
This continuation scopes shared Transcriptome metadata, Solo aggregate objects,
feature summaries/pointer arrays and heap-created streams, while preserving
chunk-local counts and algorithm-facing raw views. C++17 remains the default;
CUDA and long reads are disabled in these builds.

- Native Windows Release CTest: **98/98**. Linux Release and ASan/UBSan CTest:
  **100/100** each. Miniature reference comparisons passed on all three builds,
  including ordinary/WASP/two-pass lifecycle paths and exact matrices/axes for
  Gene, GeneFull, GeneFull_ExonOverIntron, GeneFull_Ex50pAS and Velocyto.
- The tiny Solo fixture previously used homopolymer UMI `AAAA`, filtered by the
  default policy, so empty-matrix equality was insufficient evidence. It now
  uses `ACGT` and explicitly requires positive Gene counts. No production
  filtering policy was relaxed. Failed exploratory logs remain preserved.
- LeakSanitizer with a two-thread, positive-count five-feature Solo fixture:
  preserved pre-change binary **87926 bytes / 148 allocations**; final candidate
  **zero reported leaks**, exit 0. Receipts: `aggregate-before-positive-leak.log`
  and `aggregate-after-leak.log` under the local qualification directory. No
  full-CLI leak suppression was used for this targeted check.
- The first 100000 synchronized-pair native comparison passed the full scientific
  contract: BAM multiset/scientific header, SJ, declared raw/filtered/EM matrices,
  axes, Velocity layers and scientific final fields. The final whitelist-stream
  cleanup is independently rechecked in `aggregate-final-100k-20261007/result.json`.
  Single-run timing is not a performance acceptance test; reference/candidate
  language versions, run order and cache state are confounded.

Local logs use `aggregate-*` under `data/validation/qualification-20261007/`.
The standalone filtering experiment failed on a pre-existing single-barcode
boundary; the attempted normal-return change was withdrawn. That mode is not
qualified by this batch. Rare SmartSeq/Transcript3p paths, fatal-exit unwinding,
hosted platform/CUDA checks and noise-calibrated scaling remain unverified.
No C++20 default promotion or binary release is part of this continuation.

## Standalone filtering / SmartSeq follow-up

The single-cell failure was reproduced before changing production code: the
program exited successfully but did not write matrix/barcode outputs. The
underlying final-index/cell-count confusion is corrected, and the absent-read
median access is guarded. Successful filtering now unwinds scoped resources.

- Four independently calculated standalone matrices pass: single cell,
  sparse/shuffled columns including the last cell, TopCells cutoff and rounding.
  All four also pass under ASan/UBSan with leak detection enabled.
- Two-cell single-end SmartSeq Exact and NoDedup fixtures require positive,
  hand-computed Gene/GeneFull counts and barcode axes. Linux also compares all
  emitted matrices/axes with the preserved reference. Windows intentionally
  uses the independent oracle: both the old reference and the pre-fix candidate
  dropped input and lost file markers in the multi-file preprocessing path.
  The corrected path passes; comparing two broken versions was not acceptance.
- Targeted two-thread SmartSeq Exact and NoDedup runs also pass LeakSanitizer
  with detect_leaks=1, exit 0, including owned redistribution/manifest streams.
- Windows CTest **98/98**, Linux Release and ASan/UBSan **100/100** each.
  Extended tiny mapping/quantification regression passes on all three builds,
  preserving ordinary Gene/GeneFull/Velocity reference counts/axes and existing
  lifecycle fixtures. Scientific comparator and CI-contract unit checks pass.

Local receipts use `filtering-*` and `smartseq-lsan-*` under the qualification
directory. WSL FIFO temporary paths use its Linux filesystem, while evidence
remains on the persistent workspace volume. Exploratory failures remain local.
This follow-up does not claim a new 100k/full-library performance qualification,
paired SmartSeq coverage, Transcript3p, EmptyDrops or malformed matrix coverage,
hosted CI success, C++20 promotion, commit/push or release publication.

## Paired SmartSeq / malformed matrix / Transcript3p continuation

Before repair, a truncated matrix entry still exited successfully, and standalone
Transcript3p emitted a zero-transcript/zero-entry matrix because its matching
dependency was not enabled. These were independent correctness failures, not
ownership/performance gains. Both were repaired and checked with independent
oracles instead of adopting the defective old outputs.

- Native Windows CTest **98/98**; Linux Release and ASan/UBSan **100/100** each.
- Expanded tiny regressions cover two-cell SE and PE SmartSeq, Exact/NoDedup,
  nonzero hand-calculated Gene/GeneFull counts and axes. Linux also matches the
  preserved reference. Ordinary mapping/Gene/Velocity reference checks remain.
- **19** matrix rejection fixtures cover incomplete/header/dimension errors,
  invalid coordinates, negative/non-finite/overflow counts, duplicate/extra
  entries, short/long axes and inflated size claims. They require exit **102**,
  an explicit input diagnostic and no filtered count matrix. Duplicate syntax
  is classified as unsupported input, not an invalid MatrixMarket specification.
- Valid integer/real matrices preserve the previous four hand-computed oracles.
  Their ASan/UBSan runs use detect_leaks=1. Fatal-input fixtures use leak detection
  disabled because exit does not unwind all CLI storage; their success is
  rejection safety, not leak-clean error recovery.
- Transcript3p alone produces exactly one finite count for a unique transcript/
  one-UMI fixture, with a two-transcript row axis. Missing prerequisites,
  empty/unmatched/zero/truncated/conflicting clusters and no calibration signal
  fail explicitly. This is not empirical estimator/ambiguous-transcript validation.
- Targeted PE SmartSeq Exact/NoDedup and valid Transcript3p, two threads, pass
  ASan/UBSan + LeakSanitizer detect_leaks=1, exit 0 with no reported leaks.

Local receipts: `edge-*-ctest.log`, `edge-*-mini.log`, `edge-*-standalone.log`,
`paired-smartseq-lsan-*.log` and `transcript3p-lsan.log`, under the qualification
directory. No new full-library, GPU, hosted-platform, performance or release
acceptance is claimed. C++17 remains the default; fatal cleanup, EmptyDrops
boundary cases and real/ambiguous-transcript estimator qualification remain open.

## Non-C++-upgrade continuation: EmptyDrops and CUDA17

EmptyDrops had a reproduced heap-buffer-overflow on a ten-barcode fixture when
the candidate cap exceeded available barcodes. The pre-repair ASan diagnostic
is preserved as `remaining-before-emptydrops.log`. Selection now bounds the
candidate interval by nCB, widens addition, and checks the empty interval before
decrementing. Ambient accumulation rejects overflow, and factorial size and
simulation iteration no longer wrap at uint32 limits. Knee count thresholds use
uint32 directly instead of an out-of-range signed conversion; a maximum-count
fixture independently checks the selected cell and its count.

SGT failure now logs an explicit knee-only fallback. Frequency estimates are
cached only for observed categories, removing the maximum-count-sized cache.
This does not change estimates or RNG seeds on supported inputs. Tests require
the actual simulations to finish (not just a successful early return), cover
the no-candidate and maximum-cap intervals, compare repeated output counts and
axes, and verify insufficient-SGT fallback. Nine malformed filter parameter
sets require explicit rejection. Positive extra-cell detection sensitivity is
not qualified by these tests: their FDR/simulation settings do not test rescue.

The optional seed benchmark initially failed to link after the Genome owner
change; its target now includes the real SharedMemory implementation. A fresh
Windows CUDA13.3/MSVC19.44 build uses **host17/device17**, sm_120, without an
unsupported-compiler override. Its **98/98 CTest** cases pass, actual remap matches
CPU BAM/SJ, required-without-device rejects, and auto fallback preserves outputs.
Seed batches 1/7/256/65536 and malformed-capture/no-device controls pass. Targeted
remap and 200-query seed Compute Sanitizer runs report **zero memory errors**.
No whole-mapper GPU acceleration, speed gain or race/leak proof is inferred.

CPU before/after 100K-prefix checks and a separate CPU/CUDA17-required check pass
the full scientific contract, including exact BAM/header, scientific statistics,
raw/filtered/EM/Velocity matrices and axes/SJ. Their immutable receipts are
`remaining-final-100k-20261007/result.json` and
`remaining-cuda17-100k-20261007/result.json` under data/validation. They identify
the binaries executed before the final unsigned-knee correction; they are not
retroactively attributed to the final candidate. A fresh final-candidate triplet
is recorded separately. Timing overlaps builds and remains unqualified.

Receipts beginning `remaining-cuda17-*`, `remaining-emptydrops-lsan.log` and the
updated `edge-*` logs are local evidence, not release files. Windows CPU, Linux
Release and ASan/UBSan miniature regressions preserve the ordinary reference
counts/axes and include independent repaired-path oracles. Targeted normal-path
leak checks pass; intentional fatal exits remain outside leak-cleanup claims.

C++17 defaults, release naming and supported languages are unchanged. Required
hosted macOS/s390x jobs for this exact snapshot, full-library accuracy/capacity,
noise-qualified repeated performance, fatal/cancellation cleanup and wider IPC/
allocator-failure cases remain separate acceptance gates. No commit/push or
release is performed by this continuation.

### Frozen final candidate verification

- Windows CPU **98/98**, Linux Release **100/100**, ASan/UBSan **100/100** root
  CTest cases pass. All three extended miniature regressions pass, including the
  new uint32 knee oracle, EmptyDrops simulation/no-candidate/SGT checks and prior
  SmartSeq/Transcript3p/input-validation cases. Valid standalone filtering and
  EmptyDrops targeted detect_leaks=1 checks pass with no reported leaks.
- Final Windows CUDA **host17/device17** root CTest **98/98**, actual remap/seed
  integration and targeted device memcheck pass. Six qualification/comparator/
  CI-contract Python tests and changed-script compilation checks pass locally.
- `data/validation/remaining-frozen-final-100k-20261007/result.json` records three
  fresh runs: frozen baseline, final CPU and final CUDA required. All **100,000
  paired prefix reads** pass the full scientific contract exactly. CUDA records
  `GPU_SJDB_REMAP status=0`, **24,172,833,228** device bytes and **180** chunks.
  Peak working sets are recorded per run; one run per binary cannot establish a
  repeatable speed or memory gain. Prior failed A/A calibration remains binding.
- `source-runtime-remaining-final.json` and its archived source/configuration
  companions under the qualification directory bind final source and executable
  hashes. Earlier candidate receipts remain separate and were not overwritten.

These close the named bounded checks, not every non-language-upgrade acceptance
gate in the continuation table. In particular, no full-library/representative
truth, hosted-platform, cancellation/fatal cleanup or calibrated performance
acceptance is substituted by this triplet.

### macOS manifest cleanup and s390x BAM repair

Run `37676474041` exposed two production defects after the earlier local checks:

- A single-end SmartSeq manifest leaves the default two `readFilesIn`
  placeholders intact. Cleanup used that placeholder count instead of the actual
  `readFilesNames` count and could wait on an uninitialized second PID. Initialize
  every producer PID and close only the input streams actually opened. Preserve
  failed-producer rejection and report its exit code, signal or wait error.
- STAR's private BAM buffers use native core/CIGAR words. Direct BGZF writes
  therefore produced big-endian BAM on s390x, starting with an invalid header
  length. Encode header words and numeric auxiliary fields with HTSlib's
  little-endian helpers; convert native core/CIGAR words at the final output
  boundary for unsorted, transcriptome and both coordinate-sort paths. Private
  sorting layout stays native. Little-endian hosts retain direct buffer writes.

New tests check literal BAM bytes, truncated records, two concatenated native
records read back by HTSlib, and initialized producer ownership. The s390x
harness runs these BAM tests for both language configurations. Local Windows
C++17 passes 101 tests and frozen-baseline miniature comparisons; Linux Release
passes 103 tests. Hosted macOS/s390x acceptance must be tied to the new commit's
workflow result; the preceding failed run is retained as the reproduction.

### Hosted acceptance and APT stall

Commit `260f16a` in Build and Test run `37681369483` passed all four macOS
configurations, both Windows configurations, all four Linux compiler/language
configurations, the frozen genome-index comparison and s390x. The s390x job
completed at 22:13 UTC after approximately 112 minutes under QEMU.

The remaining ASan job `112997974761` had not reached compilation: its live
log showed `apt-get update` repeatedly ignoring Azure Ubuntu mirror indexes,
with some successful HTTPS fallback indexes, for almost two hours. This is
consistent with runner-images issue 14594 and its upstream fix 14643; the
observed log cannot distinguish DNS, connection or server response failures.
Local ASan/UBSan passed 103 tests and the miniature alignment/count contract.
The real 100,000-read-pair Windows prefix comparison also preserved every
declared raw artifact. Neither substitutes for the remaining hosted ASan gate.

Bound APT requests to one retry and 15-second HTTP/HTTPS timeouts, bound the
update/install commands and add a ten-minute hosted installation-step limit.
Keep package/update failures fatal. Apply the same acquisition settings to
the Debian s390x harness, with a larger installation budget under emulation.
References: https://github.com/actions/runner-images/issues/14594 and
https://github.com/actions/runner-images/pull/14643.

### BGZF test initialization race

Release run `37696246253` passed hosted ASan but exposed a BGZF unit-test race
in the rebuilt C++17 half of the Intel macOS language comparison. The test
started writer threads before enabling on-the-fly indexing, contrary to the
documented ordering in `source/htslib/htslib/bgzf.h`. Depending on scheduling,
the writer could skip initial index allocation and then observe indexing
enabled while processing blocks, producing an invalid index. Initialize the
index before `bgzf_mt`, matching HTSlib's own `bgzip` implementation. Keep the
full random-access assertion and both payload types; production code is
unmodified by this test repair. The failed release remains unpublished.
