# Upstream PR Cherry-Pick Plan

## Status: Pending
Last updated: 2026-04-13

Reviewed all 16 open PRs on alexdobin/STAR. 6 are worth adopting, 10 are not relevant.

---

## Adopt — Bug Fixes (Critical)

### PR #2163 — Remove OOB write in `sjdbInsertJunctions.cpp`
- **Author:** eternal-flame-AD
- **What:** Removes a guaranteed out-of-bounds write in `PackedArray::writePacked()` during genome generation. The SA buffer is allocated with `nSA` elements but the code writes to index `nSA` (one past the end).
- **Why adopt:** This is a **memory corruption bug** confirmed by Valgrind. Can cause segfaults depending on heap layout. One-line fix (delete 4 lines in `sjdbInsertJunctions.cpp`).
- **Risk:** None. Removing the write is strictly correct.
- **Files:** `source/sjdbInsertJunctions.cpp` (4 lines removed)
- [ ] Cherry-pick and verify genome generation still works

### PR #535 — Fix segfault in suffix array lookup
- **Author:** Nigel Delaney (evolvedmicrobe)
- **What:** Fixes a segfault in `ReadAlign_maxMappableLength2strands.cpp` caused by an unreliable shortcut that sets incorrect SA search bounds. When a prefix of length L is not found but L-1 is, the code takes a shortcut to avoid binary search — but the shortcut can point to a position near the end of the genome that can't produce a valid alignment, leading to unsigned integer underflow and SIGSEGV.
- **Why adopt:** Reproducible crash on certain genomes (confirmed on rabbit). The fix removes the unreliable shortcut and lets the binary search handle the bounds correctly. **This is in a file we already modified** (`ReadAlign_maxMappableLength2strands.cpp`) so needs careful merge.
- **Risk:** Low — replaces a shortcut with correct binary search. May have minor performance cost (extra binary search steps in rare cases).
- **Files:** `source/ReadAlign_maxMappableLength2strands.cpp` (+3/-7 lines)
- [ ] Cherry-pick with conflict resolution against our existing changes
- [ ] Verify with smoke test

### PR #2676 — Fix memory leaks in `sjA`/`sjFilter`/`sjChunks`
- **Author:** Liam Keegan
- **What:** Fixes memory leaks where splice junction arrays were allocated locally but never freed. Also conditionally allocates `P.sjAll` vectors only when Solo SJ feature is requested, reducing memory usage.
- **Why adopt:** Clean memory leak fix. Reduces memory footprint when not using Solo SJ.
- **Risk:** None. Straightforward delete[] additions.
- **Files:** `source/outputSJ.cpp` (+13/-6 lines)
- [ ] Cherry-pick directly

---

## Adopt — Performance (High Value)

### PR #791 — Mapping speed improvement: FastResetVector + PackedArray bitmask
- **Author:** Sebastian Uhrig (suhrig)
- **What:** Two optimizations:
  1. **FastResetVector** replaces `memset` on `winBin` array (~200KB zeroed per `stitchPieces` call). Tracks which elements were actually written and only resets those. Huge win when the array is sparse (typical case).
  2. **PackedArray bitmask** replaces expensive bit-shifting `(a1>>S)<<wordCompLength)>>wordCompLength` with a simple bitmask operation in the hot `operator[]()`. Measured 1-2% improvement.
- **Why adopt:** Author benchmarked **10-25% throughput improvement** on human data. This is the same `winBin` array in `ReadAlign.h` that our code touches. The `FastResetVector` approach is clean and well-reasoned.
- **Risk:** Medium — touches `ReadAlign.h`, `ReadAlign.cpp`, `ReadAlign_stitchPieces.cpp`. Need to verify no edge case where `winBin` is densely populated (author notes `alignWindowsPerReadNmax=10000` but typical usage barely reaches 100).
- **Files:** New `FastResetVector.h`, modified `PackedArray.h`, `ReadAlign.cpp`, `ReadAlign.h`, `ReadAlign_createExtendWindowsWithAlign.cpp`, `ReadAlign_stitchPieces.cpp`
- [ ] Cherry-pick with conflict resolution
- [ ] Benchmark with smoke test (1M reads) before/after
- [ ] Benchmark with full run to confirm 10-25% claim

### PR #773 — Mapping performance: skip unnecessary Transcript copies in stitchWindowAligns
- **Author:** Alexey (alexey0308)
- **What:** Adds an early-skip predicate `isAlignToSkip()` in `stitchWindowAligns.cpp` to avoid creating expensive `Transcript` object copies when the alignment will be rejected anyway. The `stitchAlignToTranscript` function modifies the Transcript, so STAR copies it before calling — but most copies are wasted because the alignment gets a bad score.
- **Why adopt:** Author benchmarked **2.4x speedup** on test subset (117 M/hr → 49.5 M/hr reversed). This is a significant algorithmic optimization, not just micro-tuning. Adds `const` correctness throughout.
- **Risk:** Medium-high — large diff (426 additions, 321 deletions in `stitchWindowAligns.cpp`), includes formatting changes mixed with logic changes. The core optimization is sound but the PR also reformats significant code. Needs careful review.
- **Files:** `stitchWindowAligns.cpp` (+426/-321), `stitchAlignToTranscript.cpp/h`, `extendAlign.cpp/h`, `Transcript.h`, several others
- [ ] Cherry-pick — may need to apply logic change only, skip formatting
- [ ] Verify output is bit-identical on smoke test
- [ ] Benchmark

---

## Evaluate — Feature (Lower Priority)

### PR #2670 — Native CRAM output support
- **Author:** Benjamin Demaille
- **What:** Adds `--outSAMtype CRAM Unsorted` and `--outSAMtype CRAM SortedByCoordinate` using HTSlib's `hts_open` + `sam_write1` APIs. Preserves existing BAM/SAM paths.
- **Why evaluate:** CRAM is the modern standard for compressed alignments. However, this PR depends on HTSlib APIs that may differ between our bundled 1.3 and the version the author used. Also, our current use case (`--outSAMtype None` for STARsolo) means this doesn't affect our primary workflow.
- **Risk:** High — requires HTSlib API compatibility verification. Touches `BAMoutput.cpp/h`, `ReadAlignChunk.cpp`, `bamSortByCoordinate.cpp` (files we've already modified for Windows).
- **Files:** 4 files, ~130 additions
- [ ] Defer until HTSlib upgrade is considered
- [ ] Evaluate if our bundled HTSlib 1.3 has the required APIs (`hts_open`, `sam_write1`)

---

## Skip — Not Relevant

| PR | Title | Reason to Skip |
|---|---|---|
| #2666 | Galaxy badges in README | Cosmetic, not relevant to fork |
| #2636 | FIFO race condition fix | Uses POSIX `kill`/`waitpid`/`SIGTERM` — not applicable on Windows (we don't use FIFO-based readFilesCommand) |
| #2617 | Allow `vW:i` tag for wasp | Niche SAM tag feature, low demand |
| #2605 | Silicon chip (Apple M1) | macOS-specific Makefile changes, we use CMake |
| #2071 | GeneBiotype as FeaturesGeneField3 | STARsolo output format tweak, low demand |
| #2029 | Typo in manual | Documentation only |
| #2011 | SAM vector tag support | SAM-as-input feature, not our use case |
| #1926 | Manual typos | Documentation only |
| #1863 | aarch64 build fix | Makefile-only, we use CMake. Our CMake already handles ARM |
| #1586 | Unbundle htslib | Opposite direction from our approach (we need bundled + patched htslib for Windows) |

---

## Execution Order

1. **#2163** (OOB write) — trivial fix, zero risk, do first
2. **#2676** (memory leaks) — small fix, zero risk
3. **#535** (segfault) — small fix, touches our modified file, needs merge
4. **#791** (FastResetVector) — performance win, needs benchmark
5. **#773** (stitchWindowAligns skip) — biggest perf win but largest diff, needs careful merge + benchmark
6. **#2670** (CRAM) — defer, evaluate after HTSlib decision

Expected combined impact of #791 + #773: **15-30% mapping speed improvement** (these stack with our Windows-specific optimizations in the perf bottleneck plan).
