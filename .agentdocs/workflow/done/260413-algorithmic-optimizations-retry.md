# Algorithmic Optimization Retry Plan

## Status: Pending
Last updated: 2026-04-13

This plan supersedes `.agentdocs/workflow/done/260413-algorithmic-optimizations.md` as the execution guide for any future optimization work.

## What already happened

| Area | State | Evidence |
|---|---|---|
| UMI connected components | **Implemented and kept** | `source/SoloFeature_collapseUMI_Graph.cpp:136-196` now uses Union-Find |
| EmptyDrops p-value counting | **Implemented and kept** | `source/SoloFeature_emptyDrops_CR.cpp:178-214` now pre-sorts simulation values and uses `lower_bound()` |
| Window-stitch pruning | **Implemented, then reverted** | commit `80a725e` added pruning; commit `57bbc3d` reverted it after output drift |

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

---

## Phase 0: Build a Differential Harness First

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

## Phase 1: Lock In the Two Landed Optimizations

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

## Phase 2: EmptyDrops Memory Reduction

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

---

## Phase 3: UMI 1MM Detection Retry, but Only After Order Is Made Explicit

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

---

## Phase 4: Treat `stitchWindowAligns.cpp` as Research, Not Routine Optimization

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

---

## Phase 5: Suffix-Array Prefetching as an Isolated Experiment

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

## Recommended PR Sequence

| PR | Scope | Why first |
|---|---|---|
| 1 | Differential harness + regression checks | Makes all later optimization work safer |
| 2 | EmptyDrops memory reduction | Best risk/reward ratio left in the plan |
| 3 | UMI pair-stream refactor only | Removes hidden order dependence before optimization |
| 4 | Optional fast UMI enumerator | Only if PR 3 proves order can be preserved |
| 5 | `stitchWindowAligns` instrumentation only | Converts guesswork into measured data |
| 6 | SA prefetch experiment | Isolated, opt-in, easy to revert |

## Success criteria

- Every merged optimization has a narrow proof of correctness
- Smoke validation remains byte-identical to the current repository baseline
- No further default-path changes land in `stitchWindowAligns.cpp` without a stronger harness
- Remaining work is split into reversible PRs rather than one broad “optimization” batch
