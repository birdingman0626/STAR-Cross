# C++20 performance backlog — deferred

Date: 2026-10-16. Status: recorded only; **not implemented**. The user requested
no further C++17 compatibility requirement. Removing existing compatibility
lanes is separate work, not performed by this document.

## Ordered experiments

1. **Output buffer reuse / ownership transfer.** `AsyncByteWriter::submit`
   currently copies each packet into newly owned storage. Compare a bounded
   reusable pool with ownership transfer. Preserve byte order, producer backpressure,
   failure propagation, finish semantics and a fixed total memory budget.
2. **Seed-search memory-level parallelism.** The existing batch executor still
   completes each request sequentially. Prototype bounded interleaving/prefetch
   of independent fixed-index requests. Restore original result order; do not
   reorder adaptive dependencies or change seed tie-breaking. Reject added work
   or index memory that outweighs reduced cache stalls.
3. **Queue notification costs.** Profile lock/wakeup frequency before replacing
   condition variables. Compare targeted notification and C++20 atomic wait or
   semaphore only where the queue state model supports them. Preserve shutdown,
   exception and no-lost-wakeup behavior; no blanket lock-free rewrite.
4. **Bounded compile-time specialization.** Evaluate common strand/packed-width
   kernels with an exact generic fallback. C++20 concepts/constexpr may improve
   interfaces, but generated code, instruction-cache size and real throughput
   decide whether specializations remain.
5. **LTO and PGO.** These are compiler optimizations, not C++20-exclusive features.
   Train on multiple workloads and evaluate independent held-out data. Record
   toolchain, effective ISA and profile identity; retain portable distribution
   settings and separate platform-specific support.
6. **Interface simplification.** Consolidate span/bit utilities and remove obsolete
   compatibility wrappers only when this reduces complexity without losing
   supported behavior. Do not claim speed from syntax changes alone.

## Rejected shortcuts and prior work

- Safe fixed-size memcpy in packed loads must not become unaligned/aliasing-unsafe
  pointer casts. Existing optimized copies can already lower to native loads.
- Do not automatically introduce modules, coroutines, ranges or parallel STL.
- Four-byte comparisons, PWL/PLA hints and stitching copy changes already have
  screening/reversion records in `LITERATURE_OPTIMIZATION_RESULTS_20261008.md`;
  revisit only with a new cost-model rationale, not new syntax.
- Effective-index reuse already exists experimentally. Improve invalidation and
  applicability rather than treating it as a new optimization.
- CPU-only C++20 does not authorize changing index/output formats or assuming
  CUDA device/toolchain compatibility.

## Execution and validation boundary

One candidate per reversible patch. First prove full scientific parity and
negative controls, then compare end-to-end time and peak memory with a frozen
baseline. Report uncertainty and retain failed attempts. The language migration's
user-authorized performance waiver does not prove future optimizations are faster.

Hosted CI quota is exhausted. Use honest local coverage while unavailable;
queued/billing-rejected jobs are not passes. Do not launch paid/replacement
infrastructure, re-enable disabled workflows, or publish without authorization.

## Local data budget

Keep compact synchronized paired-read fixtures with manifests and original read
order. Read large original libraries from the NAS when practical; use bounded,
deterministic extraction rather than copying whole datasets. Do not silently
substitute a shorter fixture into an existing receipt or equate a prefix with a
representative biological sample. Remote reads must record input identity and
network/cache conditions; local vs NAS timings are different experiments.

Generated BAM/matrix/capture/index replicas and interrupted `_STARtmp` contents
may be pruned after preserving receipts and a cleanup inventory. Keep the main
reference, small fixtures, final non-temporary results and provenance. Historical
receipts for pruned outputs remain historical claims; the raw artifacts are no
longer locally re-verifiable until regenerated.

### Cleanup inspection, 2026-10-16

Local data totals roughly 278 GiB logical size: validation artifacts about
199 GiB, the main reference 26 GiB, and two count directories about 23 GiB each.
Large validation capture/index/output files alone account for about 179 GiB.
The proposed cleanup preserves the main reference, read fixtures, non-temporary
count results, receipts, logs, source snapshots and frozen binaries. A local
candidate inventory is saved under ignored `data/cleanup-candidates-20261016.csv`.
Bulk deletion was blocked by the execution safety policy before command start;
**no data was deleted and no reclaimed-space claim is made**. The inventory is
only a candidate list, not a completed-cleanup receipt.

## Measured results — 2026-10-22 (Linux, GCC 14)

Host: Oracle Linux 10.2 KVM guest (12 vCPU AMD Ryzen 7 5825U, 42 GiB), otherwise
idle (non-STAR CPU ≤2.1% per run). All binaries from the same commit `dacdf1a`,
GCC 14.3.1, C++20, distribution default `-march=x86-64-v3`. Runner
`scripts/linux_paired_benchmark.py`: 1M-pair `tmj-condyle-1-prefix-1m-20261006`
STARsolo profile, 8 threads, *M. fascicularis* 6.0 Ensembl 116 index loaded once
with `--genomeLoad LoadAndKeep` (timings exclude genome loading; segment removed
afterwards). Every run (12 A/A, 66 A/B) reproduced the reference BAM record
multiset/header, SJ.out.tab, all Solo.out files and scientific log fields exactly.

| Comparison (variant / baseline) | Pairs | Median | Bootstrap 95% | Status |
| --- | --- | --- | --- | --- |
| A/A baseline / baseline | 5 | 1.0024 | [0.9959, 1.0082] | `CALIBRATION_PASS` (MAD 0.41%) |
| Prefetch disabled (`STAR_NO_SA_PREFETCH`) | 10 | 1.0002 | [0.9924, 1.0181] | no effect |
| LTO (`STAR_LTO=ON`) | 10 | 1.0131 | [0.9983, 1.0165] | no benefit (≈1% slower) |
| PGO (`STAR_PGO=USE`) | 10 | 0.9798 | [0.9592, 1.0180] | not established |

PGO was trained once (8.5 s, instrumented) on the held-out
`tmj-condyle-1-heldout-700k-800k-20261008` reads, never the benchmark prefix.
Profile use was confirmed (`.text.hot` present only in the PGO object). The
held-out reads come from the same library, so this is not the multi-workload
training the plan requires.

Recommendations:

1. **Suffix-array prefetch: revert.** It is hint-only and harmless, but removing it
   changes nothing measurable; keeping unproven code in the hot path is not
   justified. Retain this record if it is revisited with a different access model.
2. **LTO: keep opt-in, default OFF.** No speedup; the point estimate is slower.
3. **PGO: keep opt-in, default OFF.** A ~2% gain is plausible but the interval
   includes 1.0. Before any default: train on several workload types (bulk,
   10x v2/v3, paired/single-end), evaluate on independent libraries, repeat with
   more pairs, and settle how a portable release would ship profiles.
4. GCC `STAR_PGO=USE` must reconfigure the same build directory used for
   `GENERATE`; profiles are keyed by object path (noted in the CMake option).

Receipts: `data/validation/opt-linux-ab-20261022/` (ignored, local only).
