# Algorithmic Optimization Fast Branch Plan

## Status: In Progress
Last updated: 2026-04-13

This plan is for a **fast branch**, not the default compatibility branch.

## Branch intent

Prioritize throughput and memory efficiency over byte-identical behavior with upstream STAR 2.7.11b. This branch is for research, benchmarking, and alternative execution strategies. Any optimization here must still preserve internal correctness, but it does **not** need to preserve the current traversal order or exact output if a better global algorithm replaces it.

## Ground rules

- Do not merge this branch into the default release line without a separate compatibility review.
- Keep every optimization behind a compile-time or runtime flag until the branch stabilizes.
- Record before/after throughput, memory, and mapping-summary deltas on the 1M-read smoke dataset.
- Profile before implementing. Use `VTune`, `perf`, compiler vectorization reports, and benchmark counters to confirm the hot path.
- Keep algorithm redesigns, build/toolchain tuning, and micro-optimizations in separate commits or PRs.
- Prefer cache-friendly contiguous storage, explicit ownership of per-thread scratch memory, and reduced copy/allocator pressure.
- Treat approximate math, lookup tables, and altered statistical methods as opt-in experiments with separate validation.
- Add concurrency only when the measured bottleneck is lack of core utilization, not memory stalls or branch-heavy search.

## CPU Optimization Policy

This branch is allowed to optimize aggressively for CPU throughput, but changes still need evidence.

### Primary levers

- lower-complexity algorithms
- cache-friendly data layout
- improved code generation (`LTO`, `PGO`, architecture-tuned builds)
- SIMD-friendly loop structure
- explicit prefetch or intrinsics where measurements justify them

### Secondary levers

- branch hints
- loop unrolling
- selective inlining
- alignment and false-sharing mitigation

Secondary levers should only be used after profiling shows that bigger changes have already been addressed.

---

## Phase 0: Profile and Benchmark Baseline

**Purpose:** Establish where CPU time, cache misses, and branch misses are spent before redesigning anything.

### Add baseline reports for

- `source/stitchWindowAligns.cpp`
- `source/SoloFeature_collapseUMI_Graph.cpp`
- `source/SoloFeature_emptyDrops_CR.cpp`
- `source/SuffixArrayFuns.cpp`

### Capture

- wall time
- CPU utilization by thread
- cache-miss and branch-miss indicators where available
- compiler vectorization remarks for candidate loops
- peak memory for Solo and alignment-heavy runs

### Rule

No “fast branch” optimization is considered complete without before/after benchmark evidence.

---

## Priority 1: Replace UMI 1MM Pair Discovery with Hash-Based Neighbor Search

**Target:** `source/SoloFeature_collapseUMI_Graph.cpp`

### Current shape

Current logic sorts UMIs, scans half-buckets, and can degrade toward `O(n^2)` in dense neighborhoods.

### Fast-branch redesign

- [ ] Separate the algorithm into three stages:
  - neighbor discovery
  - directional-collapse decision
  - connected-components resolution
- [ ] Build a hash table from encoded UMI to index/count
- [ ] For each UMI, generate all 1-mismatch neighbors directly:
  - for UMI length `L`, enumerate `3L` variants
  - probe the hash table in `O(1)` average time
- [ ] Emit canonical undirected edges once
- [ ] Apply directional-collapse logic on the edge set
- [ ] Run Union-Find over the resulting graph

### Complexity target

- Old: worst-case pair discovery approaches `O(n^2)`
- New: `O(nL + E)` with fixed small `L`

### Expected impact

- Large speedup on high-saturation cells and dense UMI neighborhoods
- Lower variance in runtime across datasets
- Better SIMD and cache behavior than the current nested scan if the data layout stays contiguous

---

## Priority 2: Replace Recursive Window Stitching with DAG / DP Search

**Target:** `source/stitchWindowAligns.cpp`, `source/stitchAlignToTranscript.cpp`

### Current shape

The current search behaves like subset enumeration with transcript mutation during recursion.

### Fast-branch redesign

- [ ] Reframe alignments in a window as nodes in a DAG
- [ ] Define compatibility edges based on:
  - read-order monotonicity
  - genome-order monotonicity
  - fragment consistency
  - splice / gap admissibility
- [ ] Create a compact DP state representing the frontier needed for future compatibility
- [ ] Replace recursive include/exclude search with:
  - memoized best-path search, or
  - beam search / A* with explicit admissible heuristics
- [ ] Move transcript materialization to the end of the path search where possible
- [ ] Keep a fallback path to the legacy implementation behind a flag

### Complexity target

- Old: exponential in the number of candidate alignments in a window
- New: target polynomial or near-polynomial search over compatible states

### Notes

This is the highest-upside algorithmic change in the codebase. It is also the least likely to remain byte-identical to current STAR behavior.

---

## Priority 3: EmptyDrops Streaming Simulation

**Target:** `source/SoloFeature_emptyDrops_CR.cpp`

### Current shape

The p-value lookup is already improved, but simulation still stores a dense matrix across all counts.

### Fast-branch redesign

- [ ] Collect only the UMI counts actually queried by candidate cells
- [ ] Stream each Monte Carlo simulation once
- [ ] Store checkpoints only at needed counts
- [ ] Build sorted per-count vectors directly during simulation

### Complexity target

- Time: similar or slightly better than current exact implementation
- Memory: from `O(nSim * maxCount)` to `O(nSim * uniqueCandidateCounts)`

### Stretch goal

- [ ] Evaluate an approximate or analytic alternative to Monte Carlo for candidate tails

This stretch goal changes the statistical method and should be tracked separately from the exact algorithm branch.

---

## Priority 4: Convert Transcript Copying into Arena / Reusable State

**Target:** `source/stitchWindowAligns.cpp`, `source/Transcript.*`

### Problem

Even before changing search strategy, recursive search pays heavily for `Transcript` copying and mutation rollback.

### Fast-branch redesign

- [ ] Add a lightweight mutable transcript state for search-time use
- [ ] Use:
  - arena allocation,
  - explicit undo stacks, or
  - persistent/path-copy-lite structures
- [ ] Materialize full `Transcript` objects only for finalists

### Expected impact

- Lower allocator pressure
- Better cache locality
- Makes DP / beam-search implementation easier

### CPU-specific additions

- [ ] evaluate `alignas(64)` only for shared or SIMD-sensitive scratch data
- [ ] evaluate AoS-to-SoA conversion for search-time state if profiler shows memory layout is limiting throughput
- [ ] keep thread-local state isolated to avoid false sharing

---

## Priority 5: Build / Toolchain Optimization Lane

**Target:** build system and release configurations

### Plan

- [ ] Add an `LTO` benchmark lane
- [ ] Add a `PGO` benchmark lane using representative STAR alignment workloads
- [ ] Compare MSVC, ICX, and `clang-cl` builds on the same benchmark set
- [ ] Measure architecture-specific settings such as AVX2-enabled builds

### Why this belongs in the fast branch

- These changes can produce meaningful CPU gains without touching algorithm semantics
- They also help identify whether code-level tuning is still worth doing after better code generation

---

## Priority 6: Prefetch-Aware Suffix-Array Search

**Target:** `source/SuffixArrayFuns.cpp`

### Current shape

The algorithm is asymptotically fine, but memory-latency dominated.

### Fast-branch redesign

- [ ] Add software prefetching around genome/SA access in the inner compare loops
- [ ] Benchmark different prefetch distances per compiler
- [ ] If useful, experiment with interleaving multiple SA probes to hide latency

### Expected impact

- Small-to-moderate gain, highly hardware-dependent

### Guardrail

Keep this isolated from the larger search redesign so its effect can be measured independently.

---

## Priority 7: SIMD / Bitset Acceleration for UMI Distance Checks

**Target:** `source/SoloFeature_collapseUMI_Graph.cpp`

### Current shape

UMI distance checks are scalar and intertwined with pair scanning.

### Fast-branch redesign

- [ ] After moving to hash-based neighbor generation, benchmark whether scalar lookup is already sufficient
- [ ] If still hot, add a SIMD-accelerated batch variant for candidate-neighbor validation
- [ ] Consider bit-packed mismatch counting helpers for fixed 2-bit nucleotide encodings

### Expected impact

- Secondary to the hash redesign
- Mostly a constant-factor improvement after the main asymptotic fix

---

## Priority 8: Branch Profiling / Search Instrumentation

**Target:** `source/stitchWindowAligns.cpp`, `source/ReadAlign_stitchPieces.cpp`

### Purpose

This branch needs hard data, not intuition.

### Add instrumentation for

- [ ] recursive calls per read
- [ ] candidate alignments per window
- [ ] finalized transcripts per window
- [ ] `Transcript` copy count
- [ ] `stitchAlignToTranscript()` call count
- [ ] time spent in:
  - window stitching
  - suffix-array search
  - EmptyDrops
  - UMI correction

### Output

- [ ] Write machine-readable benchmark summaries for each run

---

## Priority 9: CPU Micro-Optimization Pass

This pass happens only after the branch has baseline reports and at least one major algorithmic win.

### Candidate work

- [ ] add `likely` / `unlikely` hints on proven biased branches
- [ ] review loop unrolling on tiny fixed-trip loops
- [ ] review selective `inline` for very small hot helpers
- [ ] inspect division/modulo on hot integer paths for cheaper formulations where semantics stay exact

### Guardrail

No micro-tuning patch should be accepted without:
- measured speedup
- codegen or profiler evidence
- unchanged behavior relative to the fast-branch baseline

---

## Suggested implementation order

| Phase | Work | Effort | Risk | Upside |
|---|---|---|---|---|
| 0 | Profiling baseline | 2-4 hours | Low | High leverage |
| 1 | Instrumentation | 2-4 hours | Low | High leverage |
| 2 | UMI hash-based redesign | 1 day | Medium | High |
| 3 | EmptyDrops streaming memory redesign | 0.5-1 day | Medium | Medium |
| 4 | Transcript-state / copy reduction | 1 day | Medium | Medium-High |
| 5 | Build / toolchain optimization lane | 0.5-1 day | Low-Medium | Medium |
| 6 | DAG / DP window stitching prototype | 2-5 days | High | Very high |
| 7 | SA prefetch experiments | 0.5-1 day | Medium | Low-Medium |
| 8 | SIMD follow-up on UMI path | 0.5-1 day | Medium | Low-Medium |
| 9 | Micro-optimization pass | 0.5 day | Medium | Low |

## Success criteria

- Fast branch shows a measurable throughput gain on the smoke dataset
- Memory usage falls in EmptyDrops and dense UMI workloads
- Window stitching no longer behaves like naive subset search
- Benchmark reports make it clear which redesigns are worth graduating into safer branches
- CPU-oriented tuning decisions are justified by profiler data, not generic optimization rules
