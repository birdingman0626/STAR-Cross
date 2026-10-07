# Remaining qualification and ownership migration plan

Reviewed 2026-10-07. This document specifies the acceptance plan, not proof that
all stages passed. Execution evidence and remaining gates are tracked in
[QUALIFICATION_RESULTS_20261007.md](QUALIFICATION_RESULTS_20261007.md).
The historical local passes are recorded in
[PROJECT_MAINTENANCE_20261007.md](PROJECT_MAINTENANCE_20261007.md).

## Audit conclusions

| Priority | Observed gap | Required correction |
| --- | --- | --- |
| P1 | `build.yml` runs miniature analysis on Linux, but Windows PR jobs only run units/WebUI and macOS only units/version | Run the same declared analysis contract on each native PR platform; qualify both language levels |
| P1 | `release.yml` gates platform builds, but does not depend on the independent-baseline or sanitizer jobs in `build.yml` | Gate publication on qualification of the exact release SHA, not the latest green run on the branch |
| P1 | s390x script disables unit tests, ignores failure of its byte-order probe, and checks only that three index files are nonempty | Assert architecture/byte order, run bounded endian/packed tests and a real mapping/count fixture; record exact comparisons |
| P1 | Both CUDA targets explicitly set `CUDA_STANDARD 17` | Distinguish host C++20/CUDA17 from host C++20/CUDA20; a global CMake standard override does not change these target properties |
| P1 | Benchmark runner executes once per binary and reports Windows RSS as `UNMEASURED` | Add paired repetitions and platform-specific memory measurement before performance acceptance |
| P1 | Raw-matrix runner does not verify all BAM/header/filtered/EM artifacts | Declare and validate the full output contract before accepting speed measurements |
| P1 | PackedArray mixes owning and borrowed storage; Genome has manual/shared-memory lifetimes | Inventory aliases and release authority before adding destructors or replacing buffers |
| P2 | `*-latest`, package-manager compiler selection and system libraries can drift | Separate pinned qualification toolchains from a moving-version compatibility lane |
| P2 | Local records identify compiler/language/dependencies but omit several runtime/toolchain details | Record standard library, OpenMP, SDK/OS, CUDA/driver/SM, exact flags, source snapshot and binary hashes |
| P2 | “Remove unaligned accesses” and “record compiler versions” are broader than the evidence | Mark the paths/toolchains actually tested; preserve unverified modes explicitly |

Existing GNU/MSVC C++17/20 local results remain useful evidence. They do not
complete the missing hosted-platform, CUDA or representative-data checks.

## Execution order and deliverables

1. **Q0 — freeze and repair the validation contract.** Create an immutable source
   snapshot, output contract, fixture manifest and toolchain manifest; repair the
   CI gaps, repeated-run harness and memory measurement. Prove negative controls
   fail before collecting acceptance results.
2. **Q1 — CPU cross-platform qualification.** Execute the matrix below on that
   snapshot. A failure blocks the affected platform's qualification.
3. **Q2 — CUDA compatibility.** Rebuild the two experiments against the same
   qualified CPU source; execute on native Windows first, then Linux/WSL.
4. **Q3 — real-data correctness and performance.** Calibrate the harness, then
   execute matched CPU and CUDA profiles. Q1 and compile-only Q2 can overlap;
   performance runs require an otherwise idle reserved machine.
5. **Q4 — ownership migration.** Establish baseline lifetimes, migrate one owner
   at a time, and repeat its affected Q1–Q3 checks. No large combined rewrite.
6. **Q5 — C++20/default and release decisions.** Decide C++20 CPU default, CUDA
   qualification, ownership completion and performance claims separately.

The whole ownership rewrite is not a prerequisite for selecting C++20: neither
RAII nor explicit borrowed views requires C++20. Only an unresolved correctness
defect relevant to the proposed release must block that release. CUDA remains an
optional experiment; a CUDA failure blocks a CUDA-qualified claim, not an otherwise
qualified CPU-only binary. Keep a tested host/CUDA language combination for it.

Each work item records `NOT_RUN`, `RUNNING`, `PASS`, `FAIL`, `INCONCLUSIVE`, or
`NOT_APPLICABLE`, with reason and evidence paths. Missing required jobs/artifacts,
unexpected skips and timeouts are not passes. Never overwrite previous evidence.

## Q0/Q1 — cross-platform CI

| Lane | Matrix | Trigger and acceptance |
| --- | --- | --- |
| Linux x86-64 | GCC and Clang; C++17/20; Release | PR: root CTest, miniature analysis, WebUI; candidate C++20 compared to same-source C++17 |
| Windows x86-64 | MSVC; C++17/20; Release | PR: same analysis/count contract, HTS BAM/CRAM, WebUI and runtime-DLL smoke test |
| macOS ARM64 + Intel | GCC/OpenMP; C++17/20; Release | Required for candidate promotion: root CTest, miniature analysis, runtime dependencies; explicit architecture check |
| Linux s390x | GCC in pinned s390x container; C++17/20 | Candidate promotion: bounded units, tiny index AND mapping/count checks under QEMU; no speed conclusion from emulation |
| Sanitizers | Linux GCC or qualified Clang; Debug and optimized-with-symbols | PR for memory/code changes: units and miniature integration under ASan/UBSan; preserve documented full-CLI leak limitation |
| Build configuration | Windows Ninja Multi-Config Debug/Release; Linux Ninja Debug/Release | Candidate promotion: verify selected flags, build/test; test portable CPU binary on actual or controlled non-AVX2 execution |
| Moving versions | Latest hosted compiler/toolkit candidate | Scheduled or manual compatibility signal; does not redefine supported minimums automatically |

Implementation targets: `.github/workflows/build.yml`, `release.yml`,
`extras/tests/scripts/build_validate_s390x.sh`, and the existing test runners.
Use reusable qualification workflows and one matrix source where practical.
Do not duplicate scientific comparators into each workflow.

- Pin runner OS labels, compiler major/minor where available, container digests
  and action SHAs. Save package inventory/runner-image identity even where hosted
  image patch updates cannot be fixed. Define minimum supported compilers only
  after both a proposed minimum and the current toolchain actually pass.
- Use `ctest --test-dir <build> --no-tests=error --output-on-failure`; selected
  s390x subsets must publish expected test names and executed counts. Establish
  measured timeout budgets; a QEMU timeout remains a failed qualification.
- Set an explicit test thread count. Run analysis at 1 and multiple threads;
  index comparison retains 1/16-thread and low-RAM cases on suitable hosts.
- Compare same-platform candidate against the frozen independently released
  baseline and same-source C++17 against C++20. Preserve baseline dependencies
  and binary hashes; rebuilding old source with moving dependencies is a separate
  control, not proof that it reproduces the released binary.
- On s390x assert `sys.byteorder == 'big'` and target architecture without `|| true`.
  Compare index bytes to same-platform baseline and test endian-independent
  serialized components against fixed fixtures. Compare BAM/count semantics
  across platforms; do not assume every index component is portable without proof.
- Run release checks on binaries from a fresh staged/downloaded artifact in a
  directory without build-tree libraries. Check shared-library/runtime resolution,
  licenses, manifest and checksums. Verify no CPU-only CUDA runtime dependency.
- Upload receipts on success and failure (`always()`), including test XML/logs,
  commands, parameters, source/toolchain/input hashes and comparison differences.
  Retain non-sensitive test data; keep real biological inputs in private storage.
- Add an aggregate required check that enumerates expected jobs and rejects absent,
  cancelled, failed or unexpectedly skipped results. Release must depend on this
  check for its exact SHA, or invoke the same reusable workflow directly.
- Prove gates with deliberate missing Velocity output, one altered count, identical
  comparison binaries, missing runtime library and a failed required lane. These
  controls must block the corresponding acceptance/publication step.

## Q2 — CUDA compatibility and correctness

The old GPU documentation is evidence for earlier binaries, not for the updated
headers/dependencies. Refresh both `star_sjdb_remap` and the separate resident
seed-search executable; the latter is not integrated STAR read alignment.

| Combination | Required evidence |
| --- | --- |
| CPU-only C++17/20 | Build without CUDA discovery/linking; required GPU mode refuses unsupported execution |
| Host17/CUDA17 | Rebuild both existing experiments as the control |
| Host20/CUDA17 | Compile all shared headers and link; execute both experiments and CPU comparisons |
| Host20/CUDA20 | Add a validated `STAR_CUDA_STANDARD` option to both CUDA targets first; independently qualify |
| Native Windows and Linux/WSL | Record and validate each OS/toolkit/host compiler/driver/GPU combination separately |
| No device / insufficient capacity / unsupported geometry | `required` fails; supported `auto` preflight fallback is explicit; no successful GPU receipt |

Start from the historically used Windows CUDA 13.3/MSVC 19.51 and Linux CUDA
13.0/GCC 13.3 combinations, after checking actual installed versions and official
support. Test a newer toolkit only as a separate lane. Do not use
`--allow-unsupported-compiler` to turn an unsupported pairing into a qualification.
Record GPU model, compute capability, driver, toolkit, nvcc, host STL, architecture
flags and runtime linkage. Specify target SMs for build-only CI; `native` requires
local device discovery and is unsuitable for an arbitrary GPU-less runner.

Execute existing `test_gpu_sjdb_integration.py` and `test_gpu_seed_integration.py`.
Require successful backend events, nonzero query counts and exact CPU field/output
comparisons. Preserve no-device, overlapping buffer, tail/chunk, width, strand,
N/spacer/junction, repetitive-query and invalid-capacity tests. Add deterministic
fault injection around allocation/copy/launch/synchronization: a mid-execution
failure must not leave a successful receipt or accepted partial result.

Run Compute Sanitizer memcheck/initcheck and applicable racecheck/synccheck with
nonzero `--error-exitcode`, first on bounded unit fixtures and then each adapter's
integration fixture. CPU ASan/UBSan covers host lifetimes separately. Racecheck
targets shared-memory hazards; supplement it with output-word ownership review
and stress tests for global-memory packed writes. Sanitizer time is excluded from
performance measurements. Use Nsight only to explain an observed bottleneck.

GPU execution uses a trusted isolated worker/manual local run. Do not let an
untrusted public pull request run on a persistent GPU workstation with private
data or credentials. Tie uploaded GPU receipts to the candidate SHA and binaries.

## Q3 — real-data scientific and performance qualification

Reuse `subset_fastq_pairs.py`, `benchmark_cpu_subset.py`, existing BAM/MTX
comparators and seed runners. Extend their contracts rather than creating a new
parallel benchmark system. Native Windows is the primary local performance lane;
Linux/WSL results are separate comparisons within each environment.

### Fixtures and controls

- Keep the existing synchronized 1M-pair prefix as an engineering smoke fixture.
  Add deterministic pair-preserving samples across a whole library using a fixed
  seed or read-ID hash; inspect the complete source when claiming whole-file CRC
  or sampling. Preserve R1/R2 names, barcode/UMI bases and quality bytes.
- Preselect 3 libraries where available, covering actual read length/quality,
  sequencing batch, depth and mapping/repeat/multimapping variation. Start with
  100K, 1M and 5M pairs, then a representative full library. If only one source is
  available, restrict the scope of the conclusion explicitly.
- Random read subsets alter UMI depth/cell calling. Use full-library evidence for
  EmptyDrops/filtered/EM claims; synthetic or small subsets test implementation
  invariants, not biological representativeness or recovery of rare cell types.
- Freeze chemistry (currently CB16/UMI12), index/FASTA/GTF/whitelist, read hashes,
  compression, parameters, seeds, output profile and threads. Copy timed inputs
  onto the same local storage before timing; NAS transfer is a separate metric.
- Freeze R = released CPU, A = candidate CPU17, B = same candidate CPU20,
  G = same candidate CUDA. R/A assesses delivery impact; A/B isolates the language
  switch; A/G isolates the experimental backend. Match compiler/ISA/dependencies
  within A/B/G. The old release's AVX settings must not confound a language claim.

### Extend the runner before using it as a gate

Add a thin repeated-run controller with predeclared order and immutable per-run
directories. Calibrate with five A/A pairs; never reuse an old run as the measured
half of a performance pair. Use ten independent A/B pairs for acceptance, with
balanced randomized order, after one untimed warmup for each binary. An initial
small-data screen may reject a candidate early; passing it is not the final result.

Report fresh-process startup/index load, alignment/count/output stages and full
wall time. Distinguish warm filesystem cache from persistent in-process GPU index
reuse. Cold-cache claims need a reproducible isolated-host protocol; otherwise
label cache state uncontrolled and do not merge it with controlled measurements.
Record CPU affinity/threads, power mode, GPU utilization, clocks/temperature,
competing workloads and storage. Do not mix profiler-instrumented runs into medians.

Linux: collect process/process-tree memory with declared shared-memory accounting.
Windows: retain a process handle and record peak working set and peak private
commit; use job/process-tree accounting where helpers exist. Report host RAM and
peak device memory separately; sampled peaks include their sampling limitation.
`UNMEASURED` remains unverified, never zero or a pass. For ownership changes add
allocation counts/peak live bytes to catch hidden per-read copies or zero-filling.

### Acceptance, fixed before measurement

1. **Correctness first:** exact integer Gene/GeneFull/Velocity counts and axes,
   splice junctions, read identities, flags, coordinates, CIGAR, sequence/quality,
   map quality and relevant auxiliary tags. Check sortedness where promised,
   transcriptome BAM and scientific log fields. Canonicalize only documented
   volatile paths/timestamps/PG fields and permitted equal-key ordering, preserving
   multiplicity. Compressed BAM byte equality is not the semantic comparator.
2. **Filtered/EM output:** compare cell identities and declared matrices. If an
   artifact is floating point, establish its baseline repeatability and tolerance
   before comparison. Never apply floating tolerances to integer counts.
3. **Proposed engineering non-regression budgets:** upper bound of a paired 95%
   bootstrap interval for median full-job time ratio at most 1.05; median peak host
   memory increase at most `max(1% of baseline, 64 MiB)`, with no new OOM. Report
   individual runs and worst cases too. These are project acceptance proposals,
   not established scientific constants or already measured noise limits.
4. Five-pair A/A median absolute relative deviation above 2%, missing memory data,
   too few complete pairs or an interval crossing the budget is `INCONCLUSIVE`.
   Fix interference/measurement and rerun the predeclared experiment; do not relax
   thresholds after seeing results or stop once a favorable round appears.
5. A speedup claim additionally requires the paired interval entirely below 1.0
   and a useful absolute saving on its stated workloads. GPU end-to-end time
   includes setup, allocation, transfer, synchronization and output. Warm resident
   kernel/search gains have their own scope; report the amortization/break-even
   workload rather than extrapolating to whole STAR.

Run the largest/full-library profile initially as correctness/capacity screening.
Apply the same repetition gate to it before claiming full-library performance;
one successful full run certifies neither repeatability nor a speedup.

## Q4 — staged ownership migration

First add a compact ownership inventory: allocation/release sites, true owner,
borrowers, lifetime boundary, pointer offsets, resize/move behavior, thread sharing
and exceptional/early-exit paths. Classify heap, mapped/shared, caller-owned and
CUDA storage. Do not add a generic resource framework or blanket `shared_ptr`.

| Stage | Objects | Work and mandatory lifetime tests |
| --- | --- | --- |
| O0 | PackedArray, Genome, BAMoutput, ReadAlign | Inventory aliases/copy sites; detect ownership transitions and missing closes; establish leak/live-allocation baseline |
| O1 | PackedArray | Separate owned byte storage from explicit const/mutable views; owned storage noncopyable, deliberate view copy, audited move; test borrow-after-own rejection/transition, self/move assignment, repeated release, zero length and owner-before-borrower destruction |
| O2 | BAMoutput | Own buffers and streams; distinguish borrowed BGZF from handles opened locally; explicit finalize reports flush/close errors, destructor only best-effort cleanup; test both constructors, disk/write failure, partial construction, sorting and thread teardown |
| O3 | Genome | Separate heap buffers and shared/mapped storage owners; detach versus remove IPC resources explicitly; preserve interior pointers/sentinels; test NoSharedMemory and each supported load/keep/remove mode, two-process attach/detach, double-release and pass2 growth |
| O4 | ReadAlign / chunk resources | Classify per-thread arenas and borrowed Genome/Transcriptome/Parameters; preserve reuse/capacity; test multiple chunks, two-pass, canceled/failed input, chimeric/long-read modes and cleanup after workers join |
| O5 | CUDA resources and globals | Scoped device buffers/streams/events with explicit synchronization and error reporting; reuse allocations; inventory process-global state and test sequential analyses in one process where that API exists |

Concrete hazards to resolve: `PackedArray::pointArray()` currently only assigns
the pointer and does not clear an earlier owning flag; `Genome::freeMemory()`
already performs explicit conditional deletion; `BAMoutput` accepts a borrowed
BGZF handle and uses interior bin pointers. Adding destructors without migrating
these transitions can cause double frees, closing another owner's stream, or
dangling views. Treat these as ownership audit targets, not proof that every
existing call path currently triggers a defect.

For each stage, first add adversarial lifetime tests, then migrate one owner and
all its borrowers together. C++17 pointer/length views are sufficient initially;
adopt `std::span` after the supported language baseline permits it. Keep shallow
view semantics explicit and preserve packed layouts. Avoid replacing uninitialized
large allocations with eagerly zero-filled vectors without measuring the cost.

Audit fatal-error control flow too: `ErrorWarning.cpp` calls `exit(errorInt)`,
which does not unwind automatic local owners. Define explicit finalization or
bounded return/exception propagation for paths whose cleanup must be guaranteed;
adding RAII alone does not fix them. Exercise intentional fatal exits in child
processes, and test in-process release paths separately. Never let cleanup errors
replace the original analysis failure or mark a partial output complete.

Run ASan/UBSan/LSan on changed lifetimes and bounded teardown fixtures. Measure
resource balance after explicit finalization; normal process exit is not teardown
coverage. Use a qualified TSan/OpenMP setup separately where available, plus
multithreaded deterministic stress tests; absence of an unsupported TSan report
does not establish race freedom. Do not remove the full-CLI leak exception until
that complete path actually passes leak detection.

Accept each O-stage only after output equivalence, relevant platform checks,
lifetime tests and the Q3 time/RAM budget pass. Revert that isolated stage on
regression, preserving its failed evidence. Namespace/type/header/global-state
cleanup stays in separate changes so it does not obscure lifetime regressions.

## Evidence bundle and promotion checklist

Produce a small `qualification.json` plus referenced receipts containing full
source SHA (or archived dirty-tree content hash), baseline/candidate binary hashes,
toolchain/dependency/fixture manifests, commands, expected/executed tests, result
status, output-contract comparisons, per-run timing/memory and known exclusions.
A commit label alone cannot identify the current uncommitted candidate.

- [ ] Q0 negative controls and repeated/memory runner checks pass.
- [ ] Q1 required jobs and staged artifacts pass for the exact candidate snapshot.
- [ ] For CUDA-qualified artifacts: Q2 GPU profiles have successful backend
  receipts and CPU equivalence; CPU-only releases record this as not applicable.
- [ ] Q3 correctness passes; performance/RAM claims have complete paired evidence.
- [ ] Each implemented O-stage satisfies its own lifetime and regression gate.
- [ ] CPU C++20 default decision states supported toolchains and remaining optional
  CUDA/legacy-mode scope; publication depends on the exact qualified snapshot.

## Official references reviewed

Use the version-specific CUDA support table for the tested toolkit. Live/latest
documentation is discovery material and does not retroactively qualify an older
toolkit. Save URL/version/retrieval date with the execution manifest.

- [NVIDIA CUDA 13.3 Windows compiler support](https://docs.nvidia.com/cuda/archive/13.3.0/cuda-installation-guide-microsoft-windows/index.html)
- [NVIDIA Compute Sanitizer tools and their coverage](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html)
- [GitHub secure Actions and self-hosted runner guidance](https://docs.github.com/en/actions/reference/security/secure-use)
