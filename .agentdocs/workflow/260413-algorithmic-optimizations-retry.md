# Algorithmic Optimization Retry Plan

## Status: In Progress
Last updated: 2026-04-13

This plan supersedes `.agentdocs/workflow/done/260413-algorithmic-optimizations.md` as the execution guide for any future optimization work.

## What already happened

| Area | State | Evidence |
|---|---|---|
| UMI connected components | **Implemented and kept** | `source/SoloFeature_collapseUMI_Graph.cpp:136-196` now uses Union-Find |
| EmptyDrops p-value counting | **Implemented and kept** | `source/SoloFeature_emptyDrops_CR.cpp:178-214` now pre-sorts simulation values and uses `lower_bound()` |
| EmptyDrops on-demand memory | **Implemented and kept** | `source/SoloFeature_emptyDrops_CR.cpp:158-196` — sparse `simByCount` instead of full 2D matrix |
| PackedArray bitmask | **Implemented and kept** | `source/PackedArray.h:30` — `(a1>>S) & bitRecMask` (1 shift + 1 AND) |
| FastResetVector for winBin | **Implemented** | `source/ReadAlign_stitchPieces.cpp:14-16` — O(modified) reset via `FastResetVector<uintWinBin>` |
| Safe early rejection in stitchWindowAligns | **Implemented** | `source/stitchWindowAligns.cpp:322-340` — skip Transcript copy when stitchAlignToTranscript would reject (rBend<=rAend, gBend<=gAend, MAX_N_EXONS) |
| Branch-and-bound pruning | **Rejected** | Upper bound ignores sjdb bonuses; no speed benefit over safe early rejection; caused ~3% output drift |
| STAR_FAST_MODE | **Removed** | All safe optimizations moved to default path; pruning rejected; no need for separate variant |

## Failure analysis

The previous plan was too broad. It grouped together:

1. Safe structural changes that preserved observable behavior.
2. Search-space pruning in `source/stitchWindowAligns.cpp`, where execution order affects which transcript becomes the incumbent best result.

The reverted pruning changed alignment output because `stitchWindowAligns()` is not a pure “maximize score” solver. Transcript insertion and replacement order, `RA->maxScoreMate`, extension scoring, and subset elimination all interact with traversal order. Any optimization that skips branches before final transcript materialization is high risk.

## Execution rules

- One optimization family per PR.
- No default-path pruning in `stitchWindowAligns.cpp` without differential proof on repository datasets.
- Reuse the existing smoke/full validation flow from `.agentdocs/workflow/260412-release-validation-test.md`.
- For every optimization, collect both:
  - correctness evidence: output equality or targeted equivalence checks
  - performance evidence: wall time and hotspot-specific counters
- Profile before changing code. Use `VTune`, `perf`, or compiler optimization reports to confirm that the target path is still a real hotspot.
- Separate algorithm changes, build/toolchain changes, and micro-optimizations into different PRs so gains can be attributed cleanly.
- Prefer cache-friendly contiguous storage and per-thread scratch buffers over pointer-heavy structures.
- Do not introduce approximate lookup tables or altered floating-point formulas in the default branch for statistical code such as `EmptyDrops`.
- Do not add extra nested parallelism or lock-free machinery to hotspot code unless profiling proves contention or load imbalance is the bottleneck.

## CPU Optimization Policy

These plans target **CPU-only** optimization on the compatibility branch.

### Allowed default-branch techniques

- lower-complexity algorithms with output-equivalence proof
- cache-friendly data layout changes
- reduced allocation / copy overhead
- build improvements such as `LTO`, `PGO`, and architecture-specific code generation
- compiler-guided vectorization when it preserves exact results

### Restricted to experiments or fast branch

- branch hints such as `likely` / `unlikely`
- manual loop unrolling
- explicit SIMD intrinsics
- software prefetching
- approximate math, lookup-table substitution, or method changes

These are allowed only when profiling shows a specific need and the effect can be isolated.

---

## Phase 0: Profile and Baseline First

**Goal:** Prove where CPU time is actually going before changing behavior.

### Deliverables

- [ ] Capture baseline hotspot evidence for:
  - `source/stitchWindowAligns.cpp`
  - `source/SoloFeature_collapseUMI_Graph.cpp`
  - `source/SoloFeature_emptyDrops_CR.cpp`
  - `source/SuffixArrayFuns.cpp`
- [ ] Record at least:
  - wall time
  - CPU utilization
  - branch-miss / cache-miss signals where the tool supports them
  - compiler vectorization remarks for any loop targeted for SIMD-friendly cleanup
- [ ] Save before/after benchmark inputs and command lines alongside the report

### Pass criteria

- Future optimization work names a measured hotspot, not just a suspected one
- Baseline numbers exist for comparison after each PR

---

## Phase 1: Build a Differential Harness First

**Goal:** Make future failures cheap to detect before touching algorithms again.

### Deliverables

- [ ] Add a repeatable optimization validation script that runs:
  - build
  - version smoke check
  - 1M-read smoke comparison from `260412-release-validation-test.md`
- [ ] Add a second script or mode that records timing for the algorithm under test
- [ ] Add targeted synthetic fixtures for:
  - UMI graph collapse
  - EmptyDrops candidate p-value calculation
  - window-stitch branch counts only

### Pass criteria

- Same output files as the current baseline on the smoke dataset
- Timing report saved for before/after comparison

---

## Phase 2: Lock In the Two Landed Optimizations

These are already in the codebase, but they are not protected by focused checks.

### 1A. UMI Union-Find regression checks

**Files:** `source/SoloFeature_collapseUMI_Graph.cpp`

- [ ] Add synthetic cases covering:
  - isolated nodes
  - single edge
  - multi-edge chain
  - merged components
  - long chain previously vulnerable to recursive DFS depth
- [ ] Verify `nConnComp` and `graphComp` assignment match the previous DFS semantics

### 1B. EmptyDrops binary-search regression checks

**Files:** `source/SoloFeature_emptyDrops_CR.cpp`

- [ ] Add a helper that can compare:
  - old linear-count logic
  - current sorted-lookup logic
- [ ] Run both implementations on the same synthetic `simLogProb` / candidate-count inputs
- [ ] Confirm equal `nLowerP`, `p`, and final `padj`

### Pass criteria

- Landed optimizations have narrow correctness tests before any new work starts

---

## Phase 3: EmptyDrops Memory Reduction

**Priority:** High  
**Risk:** Low-Medium  
**Target file:** `source/SoloFeature_emptyDrops_CR.cpp`

This is the safest next optimization because the existing code already separates:
- simulation generation (`simLogProb`, lines 153-175)
- p-value lookup (`simByCount`, lines 182-203)

### Implementation plan

- [ ] Collect the unique candidate UMI counts after candidate selection
- [ ] Sort those counts ascending once
- [ ] Replace full `simLogProb[isim][0..maxCount]` storage with streamed checkpoints:
  - keep the current RNG seeds unchanged
  - keep the current loop over `ic=1..maxCount` unchanged
  - only store the running log-probability when `ic` reaches a needed count
- [ ] Build `simByCount[count]` directly during or immediately after each simulation
- [ ] Keep the current `lower_bound()` p-value logic unchanged

### Critical constraint

Do not change:
- RNG seeding
- simulation iteration order
- floating-point accumulation order within each simulation

### Verification

- [ ] Synthetic equivalence check against the current full-matrix implementation
- [ ] Smoke dataset: identical filtered-cell set and output files
- [ ] Record peak memory before/after
- [ ] Record whether the loop is memory-bound or compute-bound before considering SIMD or micro-tuning

---

## Phase 4: UMI 1MM Detection Retry, but Only After Order Is Made Explicit

**Priority:** Medium  
**Risk:** Medium-High  
**Target file:** `source/SoloFeature_collapseUMI_Graph.cpp`

The earlier plan treated this as a simple O(n^2) to O(n*L) replacement. It is not that simple, because `process1MMpair()` mutates graph colors and directional-collapse bits during pair discovery. That means pair visitation order can affect results.

### Required preparatory refactor

- [ ] Extract pair discovery from pair application
- [ ] Introduce a baseline helper that enumerates the current pair stream in the exact existing order
- [ ] Feed that ordered pair stream into the existing `process1MMpair()` logic
- [ ] Prove that the refactor alone is output-identical before introducing a faster enumerator

### Only then attempt the faster enumerator

- [ ] Build a neighborhood-lookup enumerator for 1MM neighbors
- [ ] Canonicalize candidate pairs
- [ ] Sort the generated pairs to the same order as the baseline enumerator
- [ ] Run the same `process1MMpair()` consumer on both streams

### Stop condition

If the fast enumerator cannot reproduce the same ordered pair stream as the baseline on synthetic cases and smoke data, do not merge it.

### Verification

- [ ] Synthetic UMI fixtures stressing duplicate counts, long chains, and dense 1MM neighborhoods
- [ ] Smoke dataset equality
- [ ] Runtime comparison on large-cell / high-UMI inputs
- [ ] Check cache behavior and branch-miss behavior before attempting low-level tuning

---

## Phase 5: Treat `stitchWindowAligns.cpp` as Research, Not Routine Optimization

**Priority:** Blocked  
**Risk:** High  
**Target file:** `source/stitchWindowAligns.cpp`

### What is explicitly out of scope for the default path

- [ ] No branch-and-bound pruning
- [ ] No early rejection based on partial scores or overlap heuristics
- [ ] No DP reformulation

These changed output already and must be treated as rejected unless a stronger proof harness exists.

### Allowed work in this phase

- [ ] Add instrumentation only:
  - recursive call count
  - max recursion depth
  - finalized transcript count
  - include vs exclude branch count
  - time spent in `stitchAlignToTranscript()`
- [ ] Identify whether the real bottleneck is:
  - recursive branching
  - `Transcript` copying
  - final transcript filtering
  - duplicate/subset checks

### Future execution gate

Do not attempt another functional optimization here until:

- [ ] the differential harness is automated
- [ ] the 1M-read smoke dataset is part of the local validation loop
- [ ] any optimization can be toggled behind a compile-time experiment flag
- [ ] profile evidence shows whether the main cost is recursion, copying, validation, or cache misses

---

## Phase 6: Build / Toolchain Optimization Lane

**Priority:** Medium  
**Risk:** Low-Medium  
**Target:** build system and release configuration, not algorithm code

This lane is for CPU speedups that come from better code generation rather than changed algorithms.

### Plan

- [ ] Add an isolated benchmark configuration for `LTO`
- [ ] Add a profile-guided optimization (`PGO`) workflow using representative alignment inputs
- [ ] Compare compiler/runtime combinations already supported by the repo:
  - MSVC
  - ICX
  - `clang-cl`
- [ ] Evaluate architecture-specific flags only in controlled builds:
  - `/arch:AVX2`
  - `-march=` style tuning where applicable

### Guardrails

- Do not mix build-optimization claims with source-level algorithm changes in the same PR
- Keep portable default builds available
- Record whether gains come from better inlining, vectorization, or branch prediction

---

## Phase 7: Suffix-Array Prefetching as an Isolated Experiment

**Priority:** Low  
**Risk:** Medium  
**Target file:** `source/SuffixArrayFuns.cpp`

This remains experimental. It should not be combined with any other optimization work.

### Plan

- [ ] Add a compile-time opt-in prefetch experiment around `compareSeqToGenome()`
- [ ] Microbenchmark only this area first
- [ ] Run smoke validation after enabling the experiment

### Merge bar

Only merge if:
- output is identical
- perf gain is measurable on real alignment input
- no compiler portability regressions appear on GCC, Clang, or MSVC/ICX

---

## Phase 8: Low-Level CPU Micro-Optimizations Checklist

These are intentionally last because they are easy to overuse and hard to justify without measurements.

- [ ] review `likely` / `unlikely` hints only on branches already proven hot and biased
- [ ] review loop-unrolling opportunities only after compiler reports are checked
- [ ] review `inline` opportunities only for tiny hot helpers that are not already inlined by the compiler
- [ ] review data alignment such as `alignas(64)` only for per-thread or SIMD-sensitive scratch structures
- [ ] review AoS-to-SoA conversions only where memory layout is a demonstrated limiter

### Rule

No micro-optimization lands without:
- measured benefit
- codegen or profiler evidence
- unchanged output on the smoke validation path

---

## Recommended PR Sequence

| PR | Scope | Why first |
|---|---|---|
| 1 | Profiling baseline + differential harness | Makes all later optimization work measurable and safer |
| 2 | Regression checks for landed optimizations | Locks in the current wins |
| 3 | EmptyDrops memory reduction | Best risk/reward ratio left in the plan |
| 4 | UMI pair-stream refactor only | Removes hidden order dependence before optimization |
| 5 | Optional fast UMI enumerator | Only if PR 4 proves order can be preserved |
| 6 | `stitchWindowAligns` instrumentation only | Converts guesswork into measured data |
| 7 | Build/toolchain optimization lane | Low-risk CPU gains without algorithm drift |
| 8 | SA prefetch experiment | Isolated, opt-in, easy to revert |

## Success criteria

- Every merged optimization has a narrow proof of correctness
- Smoke validation remains byte-identical to the current repository baseline
- No further default-path changes land in `stitchWindowAligns.cpp` without a stronger harness
- Remaining work is split into reversible PRs rather than one broad “optimization” batch
