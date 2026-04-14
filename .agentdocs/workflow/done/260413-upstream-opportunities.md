# Upstream Opportunities Plan

## Status: Pending
Last updated: 2026-04-13

This plan captures worthwhile implementation candidates found by reviewing upstream `alexdobin/STAR` issues and pull requests. It focuses on items that appear compatible with this fork’s priorities:

- preserve STAR 2.7.11b output compatibility by default
- improve robustness, portability, and contributor ergonomics
- avoid high-risk algorithmic changes unless strong validation exists

---

## What is already present locally

The following upstream-derived fixes appear already implemented in this fork and do **not** need separate execution unless regression tests are added:

- `PR #2163` — OOB write fix in SA junction insertion  
  Evidence: `source/sjdbInsertJunctions.cpp` contains the OOB write removal note.
- Part of `PR #2676` — `sjA` / `sjFilter` / `sjChunks` leak cleanup  
  Evidence: `source/outputSJ.cpp` already deletes `sjA`, `sjFilter`, and `sjChunks`.

---

## Priority 1: FIFO Cleanup / Child Process Race Fix

**Upstream reference:** `PR #2636`  
**Risk:** Low  
**Value:** High

### Problem

STAR currently deletes temporary files/FIFOs before fully synchronizing `readFilesCommand` child-process cleanup. In this fork, current code still shows:

- `STAR.cpp` removes temp directories before `P.closeReadsFiles()`
- `Parameters_closeReadsFiles.cpp` uses `SIGKILL` without waiting for termination

This can race with tools writing through FIFOs and produce hard-to-debug cleanup failures.

### Planned changes

- [ ] Reorder shutdown so `P.closeReadsFiles()` runs before temp-directory deletion
- [ ] Replace or supplement immediate kill-only logic with synchronized child cleanup
- [ ] Add status handling so child-process failures are visible in logs
- [ ] Check Windows-specific compatibility because this fork uses `wincompat.h`

### Verification

- [ ] Linux test using `--readFilesCommand`
- [ ] No deleted-FIFO / leaked-FD behavior during cleanup
- [ ] No regression on Windows native path

---

## Priority 2: Reduce Solo SJ Memory Work When Not Needed

**Upstream reference:** remaining useful part of `PR #2676`  
**Risk:** Low  
**Value:** Medium-High

### Problem

`P.sjAll` and related structures should only be populated when Solo SJ output is actually requested. The leak fixes are already present, but lazy population behavior still deserves review.

### Planned changes

- [ ] Audit `outputSJ.cpp`, `SoloFeature_processRecords.cpp`, and `SoloFeature.cpp`
- [ ] Ensure `P.sjAll` is reserved/populated only for Solo SJ feature paths
- [ ] Add a memory-focused smoke check comparing:
  - Solo Gene/GeneFull runs
  - Solo SJ runs

### Verification

- [ ] Identical outputs for non-SJ workflows
- [ ] Reduced peak memory for non-SJ STARsolo runs

---

## Priority 3: Allow `vW:i` Tag in SAM Output

**Upstream reference:** `PR #2617`  
**Risk:** Low  
**Value:** Medium

### Problem

WASP-related `vW` tagging appears unnecessarily restricted to BAM-oriented output handling. This is a narrow compatibility feature for users exporting SAM.

### Planned changes

- [ ] Inspect `Parameters_samAttributes.cpp`
- [ ] Inspect SAM-writing paths in:
  - `source/ReadAlign_outputTranscriptSAM.cpp`
  - `source/ReadAlign_outputSpliceGraphSAM.cpp`
- [ ] Relax output-type restriction only where format support is actually correct

### Verification

- [ ] Existing BAM behavior unchanged
- [ ] SAM output contains expected `vW:i` tag when WASP mode is enabled

---

## Priority 4: Preserve SAM Vector Tags Through STAR

**Upstream reference:** `PR #2011`  
**Risk:** Low-Medium  
**Value:** Medium

### Problem

STAR can ingest SAM and preserve many tags, but vector tags may be dropped on round-trip. This reduces interoperability in pipeline use cases.

### Planned changes

- [ ] Audit SAM attribute parsing and writing code paths
- [ ] Add explicit support for vector/B-array tags in SAM input preservation
- [ ] Confirm behavior under `--readFilesType SAM` with `--readFilesSAMattrKeep All`

### Verification

- [ ] Round-trip test with representative vector-tagged SAM input
- [ ] No regressions for standard scalar tags

---

## Priority 5: Preserve Index Sequence in Unmapped FASTQ Headers

**Upstream reference:** Issue `#2223`  
**Risk:** Low  
**Value:** Medium

### Problem

`--outReadsUnmapped Fastx` appears to rewrite Illumina headers in a way that drops the sample index sequence. This is a usability problem for multiplexed runs.

### Planned changes

- [ ] Locate unmapped FASTQ header formatting path
- [ ] Preserve original index-sequence field where present
- [ ] Keep STAR’s mapping-status suffix intact

### Verification

- [ ] Header output retains sample-identifying index segment
- [ ] Existing unmapped FASTQ output remains parseable by downstream tools

---

## Priority 6: Native CRAM Output

**Upstream reference:** `PR #2670`  
**Risk:** Medium-High  
**Value:** Medium-High

### Why it may fit this fork

This fork already ships bundled HTSlib 1.21, so upstream’s CRAM-output direction is more realistic here than in older STAR releases.

### Caveats

- Larger code surface than the items above
- Requires real format validation, not just compilation
- Needs clear reference-handling behavior for CRAM writing

### Planned changes

- [ ] Evaluate whether a reduced-scope implementation is possible:
  - unsorted CRAM first
  - sorted CRAM second
- [ ] Audit:
  - `BAMoutput`
  - sorted-output merge path
  - parameter validation in `Parameters.cpp`
- [ ] Add explicit tests for SAM/BAM parity vs CRAM output content where comparable

### Verification

- [ ] Build on Linux, Windows, macOS
- [ ] Confirm valid CRAM via HTSlib / samtools tools
- [ ] Ensure BAM/SAM behavior is unchanged when CRAM is not requested

---

## Priority 7: Optional System HTSlib for Packagers

**Upstream reference:** `PR #1586`  
**Risk:** Medium  
**Value:** Medium

### Problem

Downstream distro packagers often prefer unbundled system libraries. This is not necessary for normal users, but it improves packaging hygiene.

### Planned changes

- [ ] Keep bundled HTSlib as the default
- [ ] Add an opt-in CMake or Make switch for system HTSlib
- [ ] Clearly document the supported version floor

### Verification

- [ ] Bundled build remains the default and unchanged
- [ ] System-HTSlib build succeeds in a clean environment

---

## Issues That Mostly Suggest Documentation / Diagnostics Work

These upstream issues are useful signals, but they do not currently justify direct code changes without a narrower repro:

- `#2603` — mapping hangs: improve diagnostics, sanitizer runs, repro harnesses
- `#2144` — STARsolo segfault: add repro-driven crash investigation workflow
- `#2663` — macOS `readFilesCommand` spawning failures: improve docs and error messages
- `#2652` — complex barcodes without whitelist: improve parameter-validation docs
- `#2659` — STARsolo from BAM: clarify support limits and required tags
- `#2600`, `#1381`, `#2677` — STARsolo vs Cell Ranger discrepancies: mostly documentation, reproducibility, and benchmarking

---

## Recommended execution order

| Phase | Item | Effort | Risk | Depends On |
|---|---|---|---|---|
| 1 | FIFO cleanup race fix (`#2636`) | 1-2 hours | Low | - |
| 2 | Solo SJ lazy-memory audit (`#2676` partial) | 1-2 hours | Low | - |
| 3 | `vW:i` in SAM (`#2617`) | 1 hour | Low | - |
| 4 | SAM vector tags (`#2011`) | 2-4 hours | Low-Medium | 3 optional |
| 5 | Unmapped FASTQ header index preservation (`#2223`) | 1-2 hours | Low | - |
| 6 | Optional system HTSlib (`#1586`) | 2-4 hours | Medium | packaging decision |
| 7 | Native CRAM output (`#2670`) | 1-3 days | Medium-High | strong validation harness |

## Success criteria

- Low-risk upstream robustness fixes are adopted without changing default mapping results
- Memory behavior improves for non-SJ STARsolo workflows
- Interoperability with SAM pipelines improves
- Any larger feature work (CRAM, system HTSlib) is isolated behind clear validation gates
