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
