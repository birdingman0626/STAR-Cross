# C++20 status and remaining work

STAR-Cross is **C++20-only** since 2920c1d. CMake rejects STAR_CXX_STANDARD=17;
use a fresh build directory. CUDA device language selection remains independent
(STAR_CUDA_STANDARD=17 or 20). Build requirements are maintained in [BUILD.md](BUILD.md).

This file replaces the completed migration checklist. Historical decisions,
failed admission attempts and measurements remain in git history and
[CXX20_PERFORMANCE_RCA.md](CXX20_PERFORMANCE_RCA.md).
Future optimization candidates belong in
[CXX20_OPTIMIZATION_BACKLOG.md](CXX20_OPTIMIZATION_BACKLOG.md).

## Completed and scoped evidence

- C++17 checkpoint: [CXX17_CHECKPOINT_20261013.md](CXX17_CHECKPOINT_20261013.md).
- On 2026-10-16 the user waived the remaining Windows calibrated performance
  admission and authorized the default switch. That waiver was not a
  NONREGRESSION_PASS. The cancelled background sampler must not be restarted.
- Fresh MSVC default20, explicit17 and restored20 each passed 124 CTest cases;
  sparse-SA=3 scientific outputs matched. Local receipt:
  data/validation/cxx20-default-promotion-20261016/star-language-pair-ycc9bmru/.
  Source signature: 163399034b1f28cc215592a78776f9e48c98cad3000f80b3407abf2781edfc72.
- Same-source language comparisons were completed on Linux GCC/Clang, Windows
  MSVC, macOS GCC and s390x before compatibility removal. Their revision and
  performance boundaries are recorded in the RCA; they do not qualify later changes.
- Recorded portability coverage includes GCC11/Clang14 and newer toolchains,
  frozen-baseline BAM/SJ/Gene/GeneFull/Velocity comparisons, index bytes across
  thread counts, malformed inputs and sanitizers. Exact profiles and receipts:
  [qualification results](QUALIFICATION_RESULTS_20261007.md).
- Unaligned data retains memcpy; byteOrder.h cross-checks std::endian.
  Public-header std:: namespace cleanup and uint macro removal have their
  own revision-scoped checks. These changes alone imply no speedup.

## Remaining verification

- Intel macOS release coverage and any unavailable hosted jobs require a new
  applicable run; older successful CI is not evidence for a later revision.
- Qualify each CUDA toolkit/host/device-standard combination that is claimed.
  CPU-only C++20 success does not establish CUDA compatibility.
- Keep complete compiler/STL/OpenMP, SDK/OS, effective ISA, flags and dependency
  records with executable and input hashes.
- Recheck representative OpenMP paths for race and thread-count invariance.
- Finish ownership work from [OWNERSHIP_AUDIT.md](OWNERSHIP_AUDIT.md) and
  [SHARED_MEMORY_ADMISSION.md](SHARED_MEMORY_ADMISSION.md); do not treat
  C++20 views as owners or atomic shared-memory admission.
- Audit callers and tests before retiring old chimeric/long-read modes;
  names containing Old do not establish dead code.

## Rules for later changes

Adopt span, specialization or concurrency features only for a measured need.
Preserve numeric widths, buffer lifetime, candidate/tie order, RNG, serialized
formats and scientific outputs. Never replace unaligned-safe loads with casts.

Run affected unit/integration/sanitizer checks and the declared scientific
contract. For performance claims freeze the binaries and compare matched
workloads with uncertainty and peak memory. Keep language/toolchain changes
separate from alignment or dependency changes. The historical waiver applies
to the migration decision, not to future speed claims.
