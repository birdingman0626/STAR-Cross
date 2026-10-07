# C++20 migration checklist

Baseline: C++17 remains the release default. `STAR_CXX_STANDARD=20`, the `cxx20`
preset and GCC/Clang CI lanes enable qualification without silently changing
the minimum supported compiler. No performance improvement is assumed from
changing the language switch alone.

## Current decision after the ownership continuation

Keep the release default at **C++17**. Named RAII owners, shared metadata,
unaligned-safe byte operations and deterministic resource release do not require
C++20. No measured hot-path benefit or release requirement currently justifies
raising the minimum. Keep the opt-in C++20 lane; validate the *current* source
revision, not the historical 93-case candidate, before any promotion.

The most relevant future use is a bounded non-owning buffer interface using
`std::span`. It does not own/free storage, extend its lifetime, or guarantee
automatic bounds checking for every access. It complements the ownership work;
it is not a replacement. See [Microsoft's span requirements and semantics](https://learn.microsoft.com/en-us/cpp/standard-library/span-class?view=msvc-170).
CUDA language/host support must be checked against the selected toolkit and host
compiler, not inferred from CPU-only success; see [NVIDIA's nvcc guide](https://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html).
Do not introduce modules, coroutines or parallel STL without a specific measured
need. The existing all-platform, scientific-equality, sanitizer and calibrated
performance gates below still apply.

## Completed prerequisites

- [x] Configurable 17/20 language level and extensions disabled.
- [x] Shared configuration/sanitizer helpers for production and unit targets.
- [x] Debug configuration no longer receives unconditional Release optimization.
- [x] Pinned independently validated release baseline and self-comparison rejection.
- [x] Hash-verified dependency downloads and actual build-version records.
- [x] RAII clipping resources, parameter initialization and packed-array checks.
- [x] Replace numeric type macros (except legacy `uint`) with width-checked aliases.
- [x] Repair the exercised unaligned loads/stores in byte-order, suffix, SJ and
  BAM paths; whole-program absence of such accesses remains unverified.
- [x] Replace removed unbounded FASTA stream extraction; add multiline and
  oversized-header integration coverage.
- [x] Locally qualify GCC 13.3 and MSVC 19.51 C++17/20: 93 CTest cases and miniature alignment,
  SJ, sorted/transcriptome BAM and Gene/GeneFull/Velocity equivalence.

See [the maintenance verification record](PROJECT_MAINTENANCE_20261007.md)
for executed checks and the remaining platform/performance boundaries.

The audited execution plan is [QUALIFICATION_PLAN.md](QUALIFICATION_PLAN.md).
It defines Q0–Q5 evidence gates and staged O0–O5 ownership work. Completed local
checks above are historical evidence for those candidates, not completion of
every platform or mode below. Large ownership refactoring and CUDA performance
promotion are independent of the CPU language-default decision.

## Qualify toolchains before changing the default

- [ ] Record a minimum compiler/standard-library matrix for GCC, Clang, MSVC,
  macOS GCC/OpenMP, s390x GCC, and the actual CUDA toolkit/host compiler pair.
- [ ] Run C++17 and C++20 with identical optimization/ISA/dependency settings on
  every supported promotion platform (local GNU/MSVC miniature checks already pass).
- [ ] Require native GCC/Clang/MSVC unit and integration passes; both macOS
  architectures and s390x must pass the five-platform release gate.
- [ ] Compile CUDA experiments with the chosen host setting; CPU and CUDA
  language versions are separate options, so test every shared-header consumer.
- [ ] Complete toolchain records: current build records contain compiler and key
  dependency versions; add STL/OpenMP, OS/SDK, effective flags and CUDA details.

## Use C++20 only where it improves an interface

- [ ] Introduce `std::span` for bounded non-owning buffers; preserve capacities,
  constness, lifetime and thread-local allocation reuse.
- [ ] Replace the remaining `uint` macro with a namespaced index type, after
  auditing POSIX's existing `uint`, overloads, file layouts and all call sites.
- [ ] Remove `using namespace std` from public headers in bounded migrations;
  compile all consumers after each migration. Avoid a simultaneous whole-repo rewrite.
- [ ] Consider `std::endian`/`std::bit_cast` where appropriate. Keep `memcpy` for
  unaligned byte storage; `bit_cast` does not make pointer dereferences aligned.
- [ ] Separate owning storage from borrowed views in `ReadAlign`, `Genome`,
  `BAMoutput` and `PackedArray`. PackedArray and BAM buffers/streams have scoped
  ownership changes and tests; whole Genome/ReadAlign migration remains pending.
  See [the ownership inventory](OWNERSHIP_AUDIT.md) for view/copy/fatal-exit limits.
- [ ] Split the large common header only when include analysis shows a useful
  reduction; keep ABI, numeric widths and serialization stable.
- [ ] Audit global statistics/thread coordination before isolating state. Race
  checks need representative OpenMP execution, not only single-thread units.
- [ ] Inventory reachable old chimeric/long-read/shared-memory modes and their
  tests before deprecating/removing them. Names containing Old are not proof of dead code.

## Scientific and performance acceptance

- [ ] Preserve candidate ordering, tie-breaking, score bounds and RNG behavior.
- [ ] Compare normalized BAM records, SJ output, Gene/GeneFull and all three
  Velocity integer matrices/axes against the frozen independent binary.
- [ ] Compare index bytes across 1/16 threads and low-RAM chunking; retain input
  and binary hashes, parameters, toolchain and execution receipts.
- [ ] Check ASan/UBSan and malformed FASTQ/SAM/packed inputs; investigate findings
  without blanket sanitizer suppression.
- [ ] Measure elapsed time and peak RSS on representative paired reads with a
  frozen index and repeat/interleave runs. Define tolerances from measured noise
  before accepting or rejecting a change; tiny fixtures cannot establish speed.
- [ ] Inspect binary ISA and runtime dependencies and test non-AVX2 execution.
- [ ] Publish the compiler minimum and migration notes, then explicitly change
  the default to 20 only after all required checks pass.

Do not combine this default change with another alignment algorithm or major
dependency change. Modules, coroutines and parallel STL have no demonstrated
benefit in the current hot path and are not prerequisites.
