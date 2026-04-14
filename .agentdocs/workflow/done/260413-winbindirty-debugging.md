# winBinDirty Crash Investigation Plan

## Status: Pending
Last updated: 2026-04-13

## Problem statement

`winBinDirty` appears to introduce a runtime crash in the window-stitching path. The current implementation is in:

- `source/ReadAlign.h:141-142`
- `source/ReadAlign_createExtendWindowsWithAlign.cpp`
- `source/ReadAlign_stitchPieces.cpp`

This fork did **not** integrate upstream PR `#791` as designed. Upstream replaced `winBin` with an incarnation-based `FastResetVector`. This fork instead kept the raw `winBin` array and added `std::vector<uint> winBinDirty[2]` as a manual dirty-index tracker.

## Current findings

### Finding 1: This is a partial, divergent implementation

Upstream PR `#791` used:

- fixed-size `FastResetVector<uintWinBin>`
- O(1)-style reset by incrementing an incarnation counter
- no dirty-index growth per write

This fork currently uses:

- raw `uintWinBin** winBin`
- `std::vector<uint> winBinDirty[2]`
- one `push_back()` for every tracked write

### Finding 2: Duplicate dirty indices are unbounded

The same bin can be pushed into `winBinDirty` many times in one read:

- `source/ReadAlign_createExtendWindowsWithAlign.cpp:33-36`
- `source/ReadAlign_createExtendWindowsWithAlign.cpp:59-62`
- `source/ReadAlign_createExtendWindowsWithAlign.cpp:68-69`
- `source/ReadAlign_stitchPieces.cpp:104-105`
- `source/ReadAlign_stitchPieces.cpp:112-113`

There is no deduplication and no bound tied to `P.winBinN`.

### Finding 3: Capacity persists after pathological reads

`winBinDirty[0].clear()` and `winBinDirty[1].clear()` in `source/ReadAlign_stitchPieces.cpp:17-18` do **not** release capacity.

Implication:

- one pathological read can force a very large `winBinDirty` allocation
- that capacity remains resident for the `ReadAlign` object for the rest of the run
- with many chunks/threads, memory blow-up can become process-wide

### Finding 4: Secondary crash surface if stale bins ever remain

If any modified bin is not reset correctly, the most dangerous downstream read site is:

- `source/ReadAlign_createExtendWindowsWithAlign.cpp:51`

```cpp
while (wB[iBin] == wB[iBin+1]) ++iBin;
```

This assumes correctly reset trailing bins. Any stale run of equal values can push the scan toward an out-of-bounds read at `iBin+1`.

## Primary hypothesis

The crash is most likely caused by **pathological growth of `winBinDirty`**:

- repeated duplicate bin tracking
- no upper bound
- retained vector capacity after `clear()`
- multiplied by worker/thread count

This is especially plausible on:

- small repetitive genomes/databases
- highly repetitive read sets
- cases with large numbers of windows or repeated merge/extend activity

## Secondary hypothesis

If the crash is not an out-of-memory / allocator failure, then the next most likely mechanism is:

- stale `winBin` state
- followed by an invalid right-window scan at `ReadAlign_createExtendWindowsWithAlign.cpp:51`

## Debugging plan

### Phase 1: Reproduce under a stress-friendly workload

- [ ] Reproduce on the dataset/workload where the crash was observed
- [ ] Add a second stress case resembling the upstream small-database/tRNA scenario mentioned in PR `#791`
- [ ] Record:
  - read count processed before crash
  - thread count
  - memory growth over time
  - whether crash is OOM, access violation, or silent abort

### Phase 2: Add narrow instrumentation

- [ ] Log per-read:
  - `winBinDirty[0].size()`
  - `winBinDirty[1].size()`
  - unique dirty-bin count per strand
  - duplicate ratio = `dirty_size / unique_count`
  - max dirty index written
- [ ] Log when dirty size exceeds thresholds such as:
  - `P.winBinN`
  - `4 * P.winBinN`
  - `16 * P.winBinN`
- [ ] Log vector capacity before and after `clear()`

### Phase 3: Add hard assertions for state safety

- [ ] Assert every pushed dirty index satisfies `idx < P.winBinN`
- [ ] Assert every reset loop index satisfies `idx < P.winBinN`
- [ ] Add a guarded check before the right-window scan:
  - ensure `iBin + 1 < P.winBinN` before dereferencing `wB[iBin+1]`
- [ ] Add an optional end-of-read verification pass that checks all bins listed in `winBinDirty` really reset to `uintWinBinMax`

### Phase 4: Run with memory/debug tooling

- [ ] Windows:
  - use debug CRT / heap checks if available
  - capture exact exception type if it is an access violation
- [ ] Clang/LLVM lane if available:
  - AddressSanitizer
  - UndefinedBehaviorSanitizer where practical
- [ ] Linux cross-check if possible:
  - ASan
  - Valgrind or massif for memory-growth confirmation

### Phase 5: Compare against the real upstream design

- [ ] Diff current fork behavior against upstream PR `#791` design
- [ ] Confirm whether the crash disappears when using the actual incarnation-based `FastResetVector`
- [ ] Measure:
  - peak memory
  - dirty/write amplification
  - runtime

## Decision gates

### If memory blow-up is confirmed

- [ ] Treat the current `winBinDirty` implementation as invalid
- [ ] Replace it with the actual upstream `FastResetVector` design rather than trying to patch the dirty list

### If stale-bin / OOB behavior is confirmed

- [ ] Fix bounds and add state-reset assertions first
- [ ] Re-run reproduction before any performance work continues

## Recommended fix direction after confirmation

Preferred fix:

- discard the custom `winBinDirty` tracker
- adopt the real upstream `FastResetVector` integration

Why:

- bounded memory footprint
- no duplicate dirty-index accumulation
- reset semantics tied to array access, not manual bookkeeping
- much closer to the original upstream optimization that was actually benchmarked

## Success criteria

- The crash is reproduced with evidence, not anecdote
- We know whether the failure mode is:
  - memory growth / OOM
  - stale state
  - out-of-bounds read/write
- A replacement or fix path is chosen based on proof
- The final implementation is validated on both normal and stress workloads
