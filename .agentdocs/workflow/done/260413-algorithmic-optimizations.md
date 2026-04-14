# Algorithmic Optimization Plan

## Status: Plan Complete — Awaiting Execution
Last updated: 2026-04-13

Analysis of STAR's core algorithms as "competitive programming problems" — identifying suboptimal complexity and proposing better approaches.

---

## Problem 1: Subset Enumeration in Window Stitching — O(2^n) → O(n² · 2^k)

**File:** `source/stitchWindowAligns.cpp`
**Current algorithm:** Exhaustive binary-tree recursion. For each alignment in a window, branch on include/exclude. Generates all 2^nA subsets and scores each.

**LeetCode analogy:** This is the **"Maximum Score Subset"** problem — given n items with compatibility constraints and scores, find the highest-scoring compatible subset. Current solution: brute-force all 2^n subsets.

**Why it's slow:**
- For nA=20 alignments per window: 2^20 = 1M recursive calls
- Each leaf does O(nExons) transcript validation
- No memoization: identical partial transcripts recomputed across branches
- No pruning: continues even when current score can't beat best

**Better approach — Bitmask DP with pruning:**

```
Problem: Given n alignments in genomic order, find the highest-scoring
         chain of non-overlapping alignments that form a valid transcript.

This is equivalent to "Weighted Job Scheduling" (O(n²) DP) or
"Maximum Weight Independent Set on an Interval Graph" (O(n log n)).
```

- [ ] **Phase A — Branch-and-bound pruning (low risk, high reward):**
  Add upper-bound estimation at each recursion level. If `currentScore + maxPossibleRemaining < bestScore`, prune the branch.
  ```cpp
  // Before line 342 (include branch):
  if (Score + dScore + remainingMaxScore[iA+1] < trA->maxScore - P.outFilterMultimapScoreRange)
      return;  // Can't beat current best, prune
  ```
  Pre-compute `remainingMaxScore[i]` = sum of max scores from alignment i..nA.
  **Expected: 2-10x speedup on complex windows with many alignments.**

- [ ] **Phase B — DP reformulation (medium risk, high reward):**
  Replace recursion with O(n²) interval-scheduling DP:
  ```
  dp[i] = max score using alignments 0..i where alignment i is included
  dp[i] = score[i] + max(dp[j] for j < i where align[j] compatible with align[i])
  ```
  With binary search on sorted endpoints: O(n log n).
  **Expected: Eliminates exponential blowup entirely.**

- [ ] **Phase C — Memoization (medium risk):**
  Cache `(iA, transcript_signature) → best_score` to avoid recomputing identical partial transcripts across different recursion paths.

**Impact:** PR #773 (upstream) partially addresses this by adding an early-skip predicate, but doesn't change the fundamental O(2^n) complexity. The DP reformulation would be a structural fix.

---

## Problem 2: PackedArray Bit Extraction — 3 shifts → 1 mask

**File:** `source/PackedArray.h:24-32`
**Current algorithm:**
```cpp
a1 = ((a1 >> S) << wordCompLength) >> wordCompLength;  // 3 operations
```

**LeetCode analogy:** "Extract k bits starting at position S from an integer" — a standard bit manipulation problem.

**Better approach:**
```cpp
a1 = (a1 >> S) & mask;  // 1 shift + 1 AND — where mask = (1 << wordLength) - 1
```

Pre-compute `mask` once in constructor. This is exactly what upstream PR #791 proposes.

- [x] Already identified in upstream PR #791 cherry-pick plan. Apply as part of that PR.

**Impact:** 1-2% on the hot inner loop (billions of calls). Small per-call, large in aggregate.

---

## Problem 3: winBin Reset — O(200K) memset → O(k) sparse reset

**File:** `source/ReadAlign_stitchPieces.cpp`
**Current algorithm:** `memset(winBin, 0, ~200KB)` called on every `stitchPieces()` invocation, even though typically only ~100 elements are actually written.

**LeetCode analogy:** "Design a data structure that supports O(1) write and O(k) clear where k = number of writes since last clear."

**Better approach — Versioned array (FastResetVector from PR #791):**
```cpp
// Instead of clearing entire array, increment a generation counter.
// On read: if element's generation != current generation, it's "zero".
// On write: set element + update generation.
// On clear: just increment generation counter. O(1).
```

- [x] Already identified in upstream PR #791. Apply as part of that PR.

**Impact:** 10-25% on alignment phase (saves 200KB × millions of memset calls).

---

## Problem 4: UMI 1-Mismatch Detection — O(n²) → O(n · 4^(L/2))

**File:** `source/SoloFeature_collapseUMI_Graph.cpp:119-134`
**Current algorithm:** Sort UMIs by half-value, then nested loop comparing all pairs where the lower half matches. Worst case O(n²) if many UMIs share the same lower half.

**LeetCode analogy:** "Find all pairs in an array with Hamming distance exactly 1" — a classic **proximity search** problem.

**Current approach (sort + scan):**
```
Sort by lower L/2 bits → O(n log n)
For each UMI, scan forward while lower halves match → O(n·k) typical, O(n²) worst
Repeat with upper L/2 bits swapped → 2× above
```

**Better approaches:**

- [ ] **Option A — Neighborhood enumeration O(n · L):**
  For each UMI, generate all L single-substitution variants (3L variants for 4-letter alphabet). Look up each in a hash set.
  ```
  For 12-base UMI: 12 × 3 = 36 lookups per UMI
  Total: O(36n) = O(n) — linear!
  ```
  Trade-off: requires hash table of all UMIs (O(n) space, already available).

  ```cpp
  unordered_set<uint32> umiSet(umiArr, umiArr + nU0);
  for (uint32 i = 0; i < nU0; i++) {
      uint32 umi = umiArr[i * stride];
      for (int pos = 0; pos < umiLen; pos++) {
          uint32 mask = 3 << (pos * 2);  // 2-bit position mask
          uint32 orig = umi & mask;
          for (int sub = 0; sub < 4; sub++) {
              uint32 variant = (umi & ~mask) | (sub << (pos * 2));
              if (variant != umi && umiSet.count(variant))
                  process1MMpair(umi, variant, ...);
          }
      }
  }
  ```

- [ ] **Option B — BK-tree O(n · c^d) where c=alphabet, d=distance:**
  Build a BK-tree (metric tree for edit/Hamming distance). Query each UMI for all neighbors at distance 1. For Hamming distance on short strings, this is efficient.

**Recommendation:** Option A (neighborhood enumeration) is simplest and guaranteed O(n·L) linear time. For 12-base UMIs with ~100K unique UMIs per cell, this turns O(10^10) worst-case into O(3.6M) lookups.

**Impact:** Eliminates O(n²) worst case in high-UMI cells. Typical cells won't notice (early-break already works), but pathological cases (high-saturation cells with many similar UMIs) improve dramatically.

---

## Problem 5: Connected Components — Recursive DFS → Union-Find

**File:** `source/SoloFeature_collapseUMI_Graph.cpp:146-174`
**Current algorithm:** Build adjacency list from edge list, then recursive DFS to find connected components.

**LeetCode analogy:** "Number of Connected Components in an Undirected Graph" — textbook Union-Find problem.

**Problems with current approach:**
1. Recursive DFS can stack-overflow on long chains (O(N) recursion depth)
2. Builds full adjacency list O(N + E) before traversal
3. Two separate phases: build graph, then traverse

**Better approach — Union-Find with path compression + union by rank:**
```cpp
// Process edges directly during 1-mismatch detection:
void process1MMpair(uint32 u, uint32 v) {
    unite(u, v);  // O(α(n)) amortized — effectively O(1)
}
// After all pairs processed:
uint32 nComponents = countDistinctRoots();  // O(n)
```

- [ ] Replace `graphNumberOfConnectedComponents()` (DFS) with Union-Find
- [ ] Merge edge processing into `process1MMpair()` — eliminate the separate graph construction phase entirely
- [ ] Eliminates recursion stack overflow risk

**Impact:** Asymptotically same O(N + E), but:
- No recursion (no stack overflow)
- No adjacency list construction (less memory, fewer allocations)
- Simpler code (~20 lines vs ~40 lines)
- Slightly faster constant factor due to cache-friendly array access

---

## Problem 6: EmptyDrops P-value — O(cand × nSim) → O((cand + nSim) · log)

**File:** `source/SoloFeature_emptyDrops_CR.cpp:190-191`
**Current algorithm:** For each candidate cell, iterate all simulations to count how many have lower log-probability. O(cand × nSim).

**LeetCode analogy:** "For each element in array A, count elements in array B that are smaller" — the **"Count of Smaller Numbers After Self"** pattern.

**Better approach — Sort + binary search:**
```cpp
// For each simulation, sort simLogProb[isim] values at the relevant UMI count
// Then for each candidate, binary_search instead of linear scan

// Pre-sort: O(nSim · log nSim) per UMI count bucket
// Query: O(cand · log nSim) total
```

Or even simpler — merge all simulation values into one sorted array per UMI count:

```cpp
// Group simulations by UMI count, sort each group
map<uint32, vector<double>> simByCount;
for (auto &sp : simLogProb)
    for (count : relevantCounts)
        simByCount[count].push_back(sp[count]);
for (auto &[k, v] : simByCount) sort(v.begin(), v.end());

// For each candidate: binary search
for (auto &cand : candidates) {
    auto &sorted = simByCount[cand.count];
    cand.nLowerP = lower_bound(sorted.begin(), sorted.end(), cand.obsLogProb) - sorted.begin();
}
```

- [ ] Pre-sort simulation values per UMI count bucket
- [ ] Replace linear scan with `lower_bound()` binary search

**Impact:** With cand=100K and nSim=10K: current is 10^9 comparisons, proposed is ~100K × 14 = 1.4M comparisons. **~700x speedup on p-value step.** However, the Monte Carlo simulation itself (O(nSim × maxUMI)) remains the dominant cost.

---

## Problem 7: Monte Carlo Memory — O(nSim × maxUMI) → O(nSim)

**File:** `source/SoloFeature_emptyDrops_CR.cpp:160-172`
**Current algorithm:** Pre-compute full simLogProb[isim][0..maxUMI] for each simulation. Stores all intermediate values.

**LeetCode analogy:** "Prefix sum array vs on-demand computation" trade-off.

**Current space:** O(nSim × maxUMI). With nSim=10K, maxUMI=10K: 100M doubles = **800 MB**.

**Better approach — On-demand evaluation:**
Since we only query `simLogProb[isim][count]` for specific `count` values (one per candidate cell), we don't need the full matrix:

```cpp
// Only compute simLogProb at the UMI counts we actually need
unordered_set<uint32> neededCounts;
for (auto &cand : candidates)
    neededCounts.insert(cand.count);

// For each simulation, only compute at needed counts (incremental)
// Sort needed counts, compute incrementally
vector<uint32> sortedCounts(neededCounts.begin(), neededCounts.end());
sort(sortedCounts.begin(), sortedCounts.end());
```

- [ ] Collect unique UMI counts from candidates (typically << maxUMI)
- [ ] Compute simulation log-probs only at those points
- [ ] Reduces memory from O(nSim × maxUMI) to O(nSim × nUniqueCounts)

**Impact:** If 5K unique UMI counts out of 10K max: 2x memory reduction. If 500 unique counts: 20x reduction.

---

## Problem 8: Suffix Array Comparison — Cache-oblivious → Prefetch-aware

**File:** `source/SuffixArrayFuns.cpp:26-45` (compareSeqToGenome inner loop)
**Current algorithm:** Character-by-character comparison between read (sequential) and genome (random access via SA pointer).

**LeetCode analogy:** Not a complexity problem — this is already O(N) which is optimal. The issue is **cache performance**: genome access is random, causing L3 misses on every `compareSeqToGenome()` call.

**Potential approaches:**

- [ ] **Software prefetching:** Before entering the comparison loop, prefetch the genome region:
  ```cpp
  __builtin_prefetch(g + SAstr + L, 0, 0);  // Prefetch genome region
  // ... then do the comparison
  ```
  On modern CPUs with 200-cycle L3 miss penalty, prefetching 1-2 iterations ahead in the binary search can hide latency.

- [ ] **Batch SA queries:** Instead of binary search one seed at a time, batch multiple seeds and interleave their binary search steps. While one seed waits for memory, another seed's comparison executes. This is the **"software pipelining"** technique used in modern sequence aligners (e.g., BWA-MEM2's batched FM-index queries).

**Impact:** Prefetching: 5-15% on alignment phase. Batched queries: 20-40% but requires significant refactoring of the seed-finding loop structure.

**Risk:** High — fundamental change to the seed-finding pipeline. Recommend prefetching only as a first step.

---

## Priority Summary

| # | Problem | Current | Proposed | Speedup | Effort | Risk |
|---|---------|---------|----------|---------|--------|------|
| 1 | Window stitching | O(2^n) | O(n²) DP or branch-and-bound | 2-100x on complex reads | High | Medium |
| 2 | PackedArray bits | 3 shifts | 1 shift + 1 mask | 1-2% | Trivial | None |
| 3 | winBin memset | O(200K) per call | O(k) sparse | 10-25% | Low | Low |
| 4 | UMI 1-mismatch | O(n²) worst | O(n·L) hash lookup | Eliminates worst case | Medium | Low |
| 5 | Connected components | Recursive DFS | Union-Find | Same O, no stack overflow | Low | Low |
| 6 | EmptyDrops p-value | O(cand × nSim) | O((cand + nSim) · log) | ~700x on p-value step | Low | None |
| 7 | EmptyDrops memory | O(nSim × maxUMI) | O(nSim × nUnique) | 2-20x memory | Medium | Low |
| 8 | SA cache misses | Random access | Prefetching / batching | 5-40% | High | High |

**Recommended execution order:**
1. #2 + #3 (via upstream PR #791) — already planned
2. #6 (EmptyDrops p-value sort) — trivial fix, huge local speedup
3. #5 (Union-Find) — cleaner code, eliminates stack overflow risk
4. #4 (UMI hash lookup) — eliminates O(n²) worst case
5. #1 Phase A (branch-and-bound) — big win with moderate effort
6. #7 (EmptyDrops memory) — reduces peak memory
7. #8 (SA prefetch) — requires profiling to validate
8. #1 Phase B (DP reformulation) — biggest structural change, do last
