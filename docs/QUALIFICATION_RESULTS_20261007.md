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

- Execute hosted GCC/Clang, Windows, both macOS architectures and s390x on a
  committed/pushed immutable SHA; the workflow has not been dispatched here.
- Establish fully pinned toolchain/container lanes, non-AVX2 execution and
  Multi-Config Debug/Release qualification. Current records do not prove them.
- Finish noise calibration before ten-pair A/B performance acceptance; use an
  uncontended machine. Whole-library random sampling, 1M/5M/full-library scaling
  and resident-GPU break-even measurements remain unqualified.
- Complete the remaining O3/O4/O5 scope with pass2 growth, per-thread arena,
  long-read/chimeric/cancellation/fatal-exit and checked GPU finalization fixtures.
  Scoped buffer ownership is not complete CLI leak freedom.

No commit, push, C++20-default promotion or release publication was performed.
