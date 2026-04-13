# Algorithmic Optimization Fast Branch Plan

## Status: Pending
Last updated: 2026-04-13

This plan is for a **fast branch**, not the default compatibility branch.

## Branch intent

Prioritize throughput and memory efficiency over byte-identical behavior with upstream STAR 2.7.11b. This branch is for research, benchmarking, and alternative execution strategies. Any optimization here must still preserve internal correctness, but it does **not** need to preserve the current traversal order or exact output if a better global algorithm replaces it.

## Ground rules

- Do not merge this branch into the default release line without a separate compatibility review.
- Keep every optimization behind a compile-time or runtime flag until the branch stabilizes.
- Record before/after throughput, memory, and mapping-summary deltas on the 1M-read smoke dataset.

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

---

## Priority 5: Prefetch-Aware Suffix-Array Search

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

## Priority 6: SIMD / Bitset Acceleration for UMI Distance Checks

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

## Priority 7: Branch Profiling / Search Instrumentation

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

## Suggested implementation order

| Phase | Work | Effort | Risk | Upside |
|---|---|---|---|---|
| 1 | Instrumentation | 2-4 hours | Low | High leverage |
| 2 | UMI hash-based redesign | 1 day | Medium | High |
| 3 | EmptyDrops streaming memory redesign | 0.5-1 day | Medium | Medium |
| 4 | Transcript-state / copy reduction | 1 day | Medium | Medium-High |
| 5 | DAG / DP window stitching prototype | 2-5 days | High | Very high |
| 6 | SA prefetch experiments | 0.5-1 day | Medium | Low-Medium |
| 7 | SIMD follow-up on UMI path | 0.5-1 day | Medium | Low-Medium |

## Success criteria

- Fast branch shows a measurable throughput gain on the smoke dataset
- Memory usage falls in EmptyDrops and dense UMI workloads
- Window stitching no longer behaves like naive subset search
- Benchmark reports make it clear which redesigns are worth graduating into safer branches
