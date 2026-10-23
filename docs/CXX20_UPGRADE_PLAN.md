# C++20 migration checklist

Current decision (2026-10-16): the user explicitly waived remaining calibrated
performance admission and requested default C++20 migration. The background
continuation is paused and no further performance experiment will be started.
CMake, Make, release preset and s390x shipping defaults now select 20; explicit
17 compatibility remains. This is a user-authorized risk acceptance, not a
`NONREGRESSION_PASS`. Historical scientific and CI evidence below remains scoped
to its recorded revision; post-change validation is recorded separately.

Post-change local validation: a fresh Ninja/MSVC 19.51.36243.0 Release directory
configured without `STAR_CXX_STANDARD` selected 20 and passed the span probe,
build and 124/124 CTest cases. Same-cache explicit 17 rebuilt and passed 124/124
cases plus exact sparse-SA=3 scientific comparison with the saved C++20 binary.
Restoring 20 rebuilt successfully and again passed 124/124 tests. Evidence:
`data/validation/cxx20-default-promotion-20261016/star-language-pair-ycc9bmru/`,
source-signature SHA256
`163399034b1f28cc215592a78776f9e48c98cad3000f80b3407abf2781edfc72`.
The seven qualification, five language-pair harness and two CI-contract tests
pass. No new CUDA or real-library performance qualification follows from these
checks. The earlier 17/17 hosted pass belongs to `ab7d33a`; hosted CI for this
default switch is a separate pending validation, not covered by that old pass.

Historical baseline: C++17 remained the release default. `STAR_CXX_STANDARD=20`, the `cxx20`
preset and GCC/Clang CI lanes enable qualification without silently changing
the minimum supported compiler. No performance improvement is assumed from
changing the language switch alone.

## Migration started: 2026-10-13

The user authorized migration after freezing a C++17 checkpoint. Published
`4a079e7` is retained; the additional exact-sort-memory-cap repair is frozen and
tagged and published as `60ea3a4`; its 23 release jobs passed and all six platform
archives are present. This checkpoint is not evidence for later migration commits.
Work proceeds separately on `codex/cxx20-migration`; it is not a released C++20
binary and does not change the shipping default yet.

First bounded change: `SeedSearchEngine::searchBatch` accepts C++20 `std::span`
views, checks strand/output extents before any result writes, and forwards to the
existing validated executor. The replay benchmark uses it under C++20. Property
tests cover serial/parallel parity, empty views, mismatched extents and an invalid
tail query without partial output. The C++17 pointer interface remains available;
there is no owning allocation, device ABI, search ordering or alignment change.
Configuration requires actual standard-library span support for a C++20 build.

The migration branch runs the existing native 17/20 matrix. The new s390x lane
builds 17 first, preserves that binary, then rebuilds 20 from a verified identical
source signature and compares the complete scientific fixtures, including span
properties. Its evidence is uploaded separately from the binary archive. Main/tag
builds still select 17 for s390x. Compilation or launching this lane is not a pass:
the recorded comparison and hosted job must actually complete successfully.
Do not promote the default before those remaining gates are resolved.

Next gates: native same-source comparison on this changed source; host20/device17
CUDA compatibility; s390x language-pair evidence; representative paired-run
elapsed/RSS checks; then an explicit default/minimum-toolchain decision. Keep the
language change separate from algorithm and dependency upgrades. The checklists
below retain historical and still-unverified requirements rather than asserting
that a green prior revision qualifies every future change.

Executed local evidence for the first span-interface change (2026-10-13):

- MSVC 19.51: 124/124 C++20 CTest cases; same-cache C++17 rebuild and 124/124
  cases; complete sparse-SA=3 scientific output comparison passes; C++20 restored.
- MSVC 19.44 + CUDA 13.3, host20/device17: both experimental adapters compile;
  span properties pass; resident CUDA seed fixture (including invalid-request
  controls) passes; required CUDA SJ remap executes and matches CPU BAM/SJ;
  missing-device/unsupported-junction rejection and automatic fallback pass.
- GCC 13.3: 127/127 C++20 CTest cases. Optimized ASan/UBSan C++20 seed properties
  pass (133370 singleton and 71230 sorted-forward checks, plus span batch negative
  controls); the C++17 sanitizer cache is restored and its binary SHA256 preserved.
- GCC 13.3 same-cache C++17/20 comparison also passes 127/127 rebuilt C++17 tests
  and the complete sparse-SA=3 scientific output contract; C++20 restored.
- Hosted platforms, same-source s390x parity and representative elapsed/RSS
  qualification remain pending here.
  No speedup or complete GPU alignment claim follows from these fixtures.

Promotion qualification in progress (2026-10-13):

- `922c485` hosted native 17/20 and s390x-20 matrix passed all 17 jobs. Same-source
  s390x 17/20 comparison is now implemented in `4e1604e`, run `38398130764`;
  completion remains a separate gate.
- A same-source native MSVC 19.51 pair on one million synchronized TMJ read pairs
  passes exact BAM/header, SJ, Gene/GeneFull/Velocity, filtered/EM and scientific
  log checks. The C++ source is unchanged between `922c485` and `4e1604e` (only
  qualification scripts/CI changed). Elapsed and peak memory from this single
  pair are screening only, not performance admission: background CPU was busy.
- The interrupted busy-machine A/A record is explicitly isolated as
  `ABORTED_BACKGROUND_LOAD`, not reused. Promotion requires a new quiet A/A
  calibration (at least five pairs) and ten alternating C++17/20 pairs. The
  A/A median absolute relative deviation must be <=2% and its paired-bootstrap
  interval must lie within [0.98, 1.02]. The C++20 interval upper bound must be
  <=1.05, and median peak-RSS increase <=max(64 MiB, 1% of reference peak).
- Per measured run, Windows background CPU mean and p95 must both be <=5%.
  Missing/invalid metrics or multiple processor groups are UNVERIFIED, not a
  pass. Calibration is bound to inputs, binary/backend, actual arguments,
  runner/helper hashes and machine identity. Any scientific difference blocks
  the performance acceptance field even if timings happen to pass.
- Native prefix qualification does not establish whole-library, cold-cache,
  every-thread-count, GPU performance or emulated-s390x performance equivalence.
  Default C++17 remains unchanged until the actual remaining gates pass.

Follow-up verification (2026-10-15):

- Run `38400883658` (`ecba0de`) completed 16 native/validation jobs successfully,
  including macOS Intel C++20. The s390x job's C++17 subset (23 tests), C++20
  subset (24 tests), same-source scientific comparison and endian index generation
  all passed. The job nevertheless failed: the host artifact uploader could not
  traverse the container-root-owned evidence directory created with mode 0700.
- The evidence directory is now explicitly mode 0755. Only this public
  qualification directory is affected; missing-artifact failures remain fatal.
  The regression checks assert its POSIX mode for successful and failed
  comparisons; five tests pass on Windows and five on Linux. A new hosted run
  must still confirm successful artifact publication before closing this gate.
- The requested fresh native measurement was completed without controlling
  other applications: five A/A pairs and ten alternating C++17/20 pairs,
  each preceded by one warmup per binary, on the one-million synchronized-pair
  TMJ prefix, eight STAR threads, full cynomolgus index, and exact full-output
  contract. MSVC 19.51 Release, AVX2/CUDA off, dependency lock and C++ sources
  are identical. `git diff 922c485 861b995 -- source test` is empty; intervening
  commits changed qualification/CI/documentation only. Native source/runtime
  capture and per-run binary/input/helper signatures are retained locally.

| Measurement | Five A/A pairs | Ten C++17/20 pairs |
| --- | --- | --- |
| Full scientific output | All identical | All identical |
| Median paired elapsed ratio (second/first) | 1.012664 | 1.012124 |
| Paired-bootstrap 95% interval | [0.924817, 1.013528] | [1.004932, 1.064893] |
| Median paired peak-RSS delta | +20 KiB | -220 KiB |
| Memory gate | PASS | PASS |
| Background CPU mean range, measured runs | 14.51–29.10% | 11.09–27.41% |
| Background CPU p95 range, measured runs | 30.77–92.52% | 19.41–93.82% |
| Performance acceptance | UNVERIFIED_ENVIRONMENT | UNVERIFIED_ENVIRONMENT |

The A/A interval fails its predeclared [0.98, 1.02] bound. The C++20 interval
upper bound exceeds 1.05, so non-regression is not established. The helper's
`TIME_REGRESSION` label means this admission condition failed; it is **not**
proof of an intrinsic >5% language/compiler regression. Busy-environment
telemetry and failed independent calibration prevent such a causal claim.
No time outliers were removed, thresholds relaxed, or favorable runs selected.
Observed STAR peak RSS is approximately 29.24 GiB for both modes; this excludes
OS file cache and the Python comparison process, and is not a whole-workflow
memory bound. No speedup is claimed.

Local receipts (ignored, not release assets) are
`data/validation/cxx20-promotion-aa-final-1m-20261015/result.json` and
`data/validation/cxx20-promotion-ab-final-1m-20261015/result.json`; they retain
commands, input and binary hashes, all scientific checks and OS measurements.
C++17 binary SHA256 is
`2925ce26c0ef40504e030d3032f831b45eaf644459ed91b6965bc5ae054c1ac6`;
C++20 binary SHA256 is
`a9bd580223e63a4b84c2695246ad5a6fa4923b321871c201322c1398e4b32ef0`.

At this verification, hosted runs `38742508396` (permission fix) and
`38742597777` (documentation-only successor) each have 16 successful jobs with
s390x still running. Neither is recorded as a complete hosted pass. The default
remains C++17: promotion requires retained s390x evidence **and** a new valid
quiet-machine calibration/performance result, not just successful compilation.

Evidence logs are retained locally under `data/validation/architecture-20261009/`
with the `cxx20-migration-` prefix; the source signature and binary hashes are
retained by the comparison/fixture scripts. Hosted results must be tied to the
committed migration SHA, not to the C++17 checkpoint's workflow results.

## Previous decision after the ownership continuation

Subsequent [performance RCA](CXX20_PERFORMANCE_RCA.md) records fresh CPU/I/O and
same-P-core diagnostics, an independent no-STAR ambient-load control, the new
fail-fast idle preflight, and successful s390x evidence publication (17/17 jobs).
The statistical failure label is now `NONREGRESSION_NOT_ESTABLISHED`; historical
receipts are not rewritten. Quiet-machine performance admission remains open.

The previous recommendation was to keep the release default at **C++17**. Named RAII owners, shared metadata,
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

- [x] Record a minimum compiler/standard-library matrix for GCC, Clang, MSVC,
  macOS GCC/OpenMP, s390x GCC. Tested floor (2026-10-23, Ubuntu 22.04): GCC 10.5,
  GCC 11.4 and Clang 14.0 pass build, CTest, miniature integration and web UI.
  Newest tested: GCC 14/15, Clang 20, MSVC 14.51, macOS 26 GCC 15, s390x/aarch64
  GCC (QEMU). Still open: the CUDA toolkit/host compiler pair.
- [x] Run C++17 and C++20 with identical optimization/ISA/dependency settings on
  every supported promotion platform: same-source language-pair qualification
  passed on Linux GCC/Clang, Windows MSVC, macOS GCC and s390x before C++17 was
  retired (`2920c1d`); see CXX20_PERFORMANCE_RCA.md.
- [ ] Require native GCC/Clang/MSVC unit and integration passes; both macOS
  architectures and s390x must pass the five-platform release gate. Done except
  Intel macOS (no hardware; hosted `macos-15-intel` pending Actions quota).
- [ ] Compile CUDA experiments with the chosen host setting; CPU and CUDA
  language versions are separate options, so test every shared-header consumer.
- [ ] Complete toolchain records: current build records contain compiler and key
  dependency versions; add STL/OpenMP, OS/SDK, effective flags and CUDA details.

## Use C++20 only where it improves an interface

- [ ] Introduce `std::span` for bounded non-owning buffers; preserve capacities,
  constness, lifetime and thread-local allocation reuse.
- [x] Replace the remaining `uint` macro: all 2026 STAR uses are now the existing
  `uint64` alias and the macro is gone (`4658f31`); objects are byte-identical.
- [x] Remove `using namespace std` from public headers (`6a09a16`): headers use
  `std::` (534 compiler-located insertions), .cpp files keep the directive; all
  1123 objects and both binaries byte-identical under GCC 14; Clang/MSVC tested.
- [x] Consider `std::endian`/`std::bit_cast`: `byteOrder.h` cross-checks its
  detection against `std::endian` at compile time; unaligned fields keep `memcpy`
  (`unaligned.h`), now also used by duplicate marking (`88588cb`).
- [ ] Separate owning storage from borrowed views in `ReadAlign`, `Genome`,
  `BAMoutput` and `PackedArray`. PackedArray and BAM buffers/streams have scoped
  ownership changes and tests; Genome, ReadAlign arenas, Transcriptome/Solo and
  standalone BAM tools now have explicit owners (OWNERSHIP_AUDIT.md). Remaining:
  shared-memory Genome admission/removal is not atomic against a concurrent new
  attach (a racing newcomer can fail to attach; no corrupted reads).
- [ ] Split the large common header only when include analysis shows a useful
  reduction; keep ABI, numeric widths and serialization stable.
- [ ] Audit global statistics/thread coordination before isolating state. Race
  checks need representative OpenMP execution, not only single-thread units.
- [ ] Inventory reachable old chimeric/long-read/shared-memory modes and their
  tests before deprecating/removing them. Names containing Old are not proof of dead code.

## Scientific and performance acceptance

- [ ] Preserve candidate ordering, tie-breaking, score bounds and RNG behavior.
- [x] Compare normalized BAM records, SJ output, Gene/GeneFull and all three
  Velocity integer matrices/axes against the frozen independent binary
  (frozen-baseline job, replayed on `66bdd1a`).
- [x] Compare index bytes across 1/16 threads and low-RAM chunking; retain input
  and binary hashes, parameters, toolchain and execution receipts.
- [x] Check ASan/UBSan and malformed FASTQ/SAM/packed inputs; investigate findings
  without blanket sanitizer suppression (`test_malformed_inputs.py`; the UBSan
  misaligned access found in duplicate marking was fixed, not suppressed).
- [x] Measure elapsed time and peak RSS on representative paired reads with a
  frozen index and repeat/interleave runs. Define tolerances from measured noise
  before accepting or rejecting a change; tiny fixtures cannot establish speed.
  (Linux GCC 14: C++20/C++17 0.989 [0.982, 0.999], A/A-calibrated.)
- [x] Inspect binary ISA and runtime dependencies and test non-AVX2 execution:
  a portable GCC 11 build has no AVX outside Parasail's runtime-dispatched AVX2
  kernels and passes the miniature integration on an emulated Nehalem CPU.
- [x] Publish the compiler minimum and migration notes, then explicitly change
  the default to 20 only after all required checks pass (BUILD.md; C++20-only
  since `2920c1d`).

Do not combine this default change with another alignment algorithm or major
dependency change. Modules, coroutines and parallel STL have no demonstrated
benefit in the current hot path and are not prerequisites.
