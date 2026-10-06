# Performance Optimization and Upstream Sync Plan

Status: scoped correctness/harness fixes implemented and verified in WSL; large-workload
performance, legacy-mode qualification and cross-platform execution remain pending.
The [review register](261006-cpu-upstream-review-register.md) records reviewed patches,
our own decisions and test evidence; consult it before repeating upstream research.
Reviewed: 2026-10-06. Pair with the [GPU roadmap](261006-gpu-acceleration-research-and-roadmap.md)
and [validation contract](261006-gpu-validation-and-benchmark-plan.md).

## 1. Baseline and scope

Freeze a source commit plus local diff, build/dependency versions, effective parameters
and input/reference hashes. Maintain separate current-default and legacy baselines.
For compression changes, require identical decompressed BAM bytes given identical
input streams; compressed file bytes may differ. For full pipelines compare semantic
records and exact integer counts. Version labels alone do not identify behavior.

The previous percentages for compression, allocator and stitching gains were unmeasured
projections. Replace them with profiling and before/after results. Optimize CPU
candidates individually so GPU comparisons use a credible CPU baseline.

## 2. CPU candidates

### A.1: optional libdeflate integration

Bundled-HTSlib CMake now supports optional STAR_USE_LIBDEFLATE (default OFF), retaining ZLIB.
Inspect the effective generated configuration too: source intent does not prove
which backend a particular installed binary uses.

- Use an explicitly installed optional libdeflate dependency and record its resolved
  version with execution evidence (this review used 1.19); avoid another automatic fetch.
- Define HAVE_LIBDEFLATE only when headers and linkage are available for that target.
  Preserve system-HTSlib builds and CPU builds without the new dependency.
- Confirm actual BGZF backend in build/runtime evidence; test block limits,
  incompressible payloads, EOF, indexes and all BAM writers.
- Measure decompressed equivalence, compression throughput/ratio and total runtime.
  Matrix-only runs cannot gain from BAM compression.
- CRAM is a separate BAM-to-CRAM transcode in source/cramOutput.cpp; measure its
  compression/codecs independently. A BGZF improvement is not a promised CRAM speedup.

### A.2: allocator experiment, only if allocation is a measured hotspot

Treat contention as a hypothesis. Measure allocation counts, sizes, contention and
peak RSS before introducing mimalloc. A proposed STAR_USE_MIMALLOC option defaults
off. Verify actual malloc/new interception and consistent allocation/free ownership,
especially across Windows runtime/library boundaries; merely linking a static library
does not prove it replaced allocation. Compare throughput and peak memory on supported
platforms and retain the existing allocator if the benefit is insignificant.

### A.3: suffix-sum bound in stitchWindowAligns

The reviewed non-legacy branch computed maxRemaining with a loop over remaining WA
entries, but omitted terminal extension and final score terms. The unproven pruning
was removed as a correctness precaution. Do not implement a suffix cache until a
complete upper bound is proven and profiling supports it.

Keep the legacy branch untouched. Prove the contribution terms, candidate order and
WA contents remain valid throughout recursion; rebuild/invalidate if any input changes.
Audit score range/overflow before choosing the cache type. Compare per-node bounds,
pruning decisions and final alignments against the current implementation. A faster
bound with changed pruning is an algorithm change, not equivalent optimization.

### A.4: EmptyDrops binary search is already implemented

source/SoloFeature_emptyDrops_CR.cpp already stores simulations per needed UMI count,
sorts each distribution and uses lower_bound. Remove this from the implementation
backlog. Retain regression coverage for the strict comparison, ties, random seeds,
simulation order, p-values and selected cells. Profile the remaining simulation cost
before proposing GPU or further CPU changes.

## 3. Upstream candidates: verify before cherry-picking

The identifiers below have now been inspected against official upstream patches and
local source. Exact inspected heads, rejection reasons and implementation decisions
are in the review register; the topic list below is only an index.

| Identifier in earlier plan | Topic to verify |
|---|---|
| PR #2692 | parametersDefault.xxd parallel-build dependencies |
| PR #2680 | genome-generation memory estimation |
| PR #2011 | SAM B-type tag preservation |
| Issue #2223 | unmapped FASTQ header/index preservation |
| PR #2071 | gene-biotype feature metadata |
| Issues #2600 / #1381 | GeneFull and Cell Ranger counting differences |

For each candidate, verify its official upstream page, exact patch/commit and license,
check whether this fork already contains the behavior, add a reproducer, then select
the smallest applicable fix. Do not blindly cherry-pick by issue number. Record
rejected/not-applicable/already-present candidates. Cell Ranger compatibility must
specify chemistry, version, reference and counting rules; no blanket byte-equality
claim follows from a GeneFull preset.

## 4. Order and acceptance

1. Baseline, actual CTest discovery, stricter artifact/comparison contract and profiling.
2. Select the measured CPU bottleneck; implement one bounded change at a time.
3. Re-run focused oracles and representative before/after workloads.
4. Reprofile before selecting a GPU offload candidate.
5. Qualify full workloads and supported platforms before a release.

Existing scripts/smoke_test.sh checks basic execution, not output equivalence.
scripts/release_compare.sh still covers a fixed historical list but now rejects absent
reference files. validate_build.sh now requires actual discovered unit tests and all
smoke inputs/reference artifacts, and can run a supplied reference executable.
Neither harness alone qualifies BAM, CRAM or Velocity; retain profile-specific gates.

Use the companion contract for BAM/junction/matrix/Velocity acceptance. Linux,
Windows, macOS and endian coverage must match the changed surface. Performance
thresholds and floating tolerances must be declared before evaluating candidates.
