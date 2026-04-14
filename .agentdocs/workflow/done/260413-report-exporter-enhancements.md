# Report Exporter Enhancement Plan

## Status: Pending
Last updated: 2026-04-13

## Goal

Expand the HTML report exporter so it surfaces more of STAR/STARsolo’s useful outputs without turning the report into a raw file dump. The report should remain fast, self-contained, and QC-oriented.

## Current state

The existing exporter in `scripts/starsolo_qc_report.sh` already reads:

- `Solo.out/Gene/Summary.csv`
- `Solo.out/GeneFull_Ex50pAS/Summary.csv`
- `Solo.out/Barcodes.stats`
- `Log.final.out`
- `UMIperCellSorted.txt` for the knee plot

This gives a good top-level STARsolo summary, but it lacks:

- run configuration/context
- feature-assignment breakdown
- progress/runtime clues
- conditional sections for optional STAR outputs

---

## Priority 1: Add Run Context from `Log.out`

**Value:** High  
**Risk:** Low

### Why

Users need to know how the run was produced, not just the resulting metrics.

### Planned changes

- [ ] Parse a compact subset of `Log.out`
- [ ] Add a “Run Configuration” section showing:
  - STAR version
  - command line or sanitized command summary
  - `genomeDir`
  - `--soloType`
  - `--soloFeatures`
  - `--soloCellFilter`
  - thread count
- [ ] Keep the section summarized; do not inline the entire log

### Verification

- [ ] Report still renders when `Log.out` is absent or incomplete
- [ ] Long command lines degrade gracefully

---

## Priority 2: Add `Features.stats` Breakdown

**Value:** High  
**Risk:** Low

### Why

`Summary.csv` gives headline metrics, but `Features.stats` explains where reads were lost or classified.

### Planned changes

- [ ] Parse `Solo.out/Gene/Features.stats`
- [ ] Parse `Solo.out/GeneFull_Ex50pAS/Features.stats` when present
- [ ] Add a “Feature Assignment” section with:
  - `nUnmapped`
  - `nNoFeature`
  - `nAmbigFeature`
  - `nAmbigFeatureMultimap`
  - `nTooMany`
  - `nExactMatch`
  - `nMatch`
- [ ] Show exonic vs gene-full side by side where available

### Verification

- [ ] Missing optional fields do not break report generation
- [ ] Numbers align with raw stats files for known test outputs

---

## Priority 3: Improve Raw vs Filtered Cell Interpretation

**Value:** High  
**Risk:** Low

### Why

The current report shows headline cell metrics but does not clearly explain the impact of cell filtering.

### Planned changes

- [ ] Highlight raw vs filtered read/cell summaries where available from `Summary.csv`
- [ ] Add small derived metrics such as:
  - reads in cells
  - UMIs per estimated cell
  - genes per estimated cell
  - exonic vs exonic+intronic delta
- [ ] Add short textual interpretation blocks for common QC patterns

### Verification

- [ ] Derived values are stable across sample reports
- [ ] The report remains readable on mobile and desktop

---

## Priority 4: Add Runtime / Progress Summary from `Log.progress.out`

**Value:** Medium  
**Risk:** Low-Medium

### Why

For long runs, progress summaries help users reason about runtime behavior and stalls.

### Planned changes

- [ ] Detect `Log.progress.out` when present
- [ ] Extract a compact summary:
  - start time
  - finish time
  - mapping speed trend or last-known speed
  - processed read milestones
- [ ] Optionally render a small chart or sparkline

### Verification

- [ ] Runs without `Log.progress.out` still generate clean reports
- [ ] Parsing tolerates minor format variation

---

## Priority 5: Add Conditional Sections for Optional Outputs

**Value:** Medium  
**Risk:** Low

### Why

Not every STAR run produces the same artifacts. The report should adapt to what exists.

### Planned changes

- [ ] Show `SJ.out.tab` summary only when splice-junction output exists
- [ ] Show chimeric-summary section only when chimeric outputs exist
- [ ] Show genome-generation metadata only for genome-generate report contexts
- [ ] Avoid hard-failing when optional files are missing

### Verification

- [ ] Conditional sections appear only when relevant
- [ ] Default STARsolo reports remain uncluttered

---

## Priority 6: Add “Details” Links Instead of Inlining Raw Files

**Value:** Medium  
**Risk:** Low

### Why

Some users need direct access to raw outputs, but the HTML should remain a report, not a file dump.

### Planned changes

- [ ] Add a “Files” or “Artifacts” section with links/paths to:
  - `Log.out`
  - `Log.final.out`
  - `Log.progress.out`
  - `Barcodes.stats`
  - `Features.stats`
  - `Summary.csv`
- [ ] Optionally use collapsible `<details>` blocks for short excerpts only
- [ ] Do not inline full `matrix.mtx` or large tables

### Verification

- [ ] Large output files do not bloat the report
- [ ] Paths are clear and useful for local users

---

## Priority 7: Improve Exporter Robustness

**Value:** High  
**Risk:** Low

### Current limitations

The current script is pure Bash and relies on simple `grep` / `awk` parsing. It is lightweight, but fragile when file formats vary.

### Planned changes

- [ ] Normalize parsing helpers for TSV- and CSV-like STAR outputs
- [ ] Make missing fields non-fatal where possible
- [ ] Add clear warnings in the generated report when expected files are missing
- [ ] Consider whether the exporter should stay Bash-only or move to a small Python helper later

### Verification

- [ ] Report generation succeeds on:
  - full STARsolo output
  - partial output missing optional files
  - old/new metric variants where field names differ slightly

---

## Out of scope

- [ ] Rendering count matrices directly in HTML
- [ ] Full BAM/SAM/FASTQ-derived visualizations
- [ ] Turning the exporter into a general-purpose STAR file browser

---

## Recommended implementation order

| Phase | Work | Effort | Risk | Depends On |
|---|---|---|---|---|
| 1 | `Log.out` run context | 1-2 hours | Low | - |
| 2 | `Features.stats` breakdown | 1-2 hours | Low | - |
| 3 | Raw vs filtered summary improvements | 1-2 hours | Low | 2 |
| 4 | Exporter robustness cleanup | 1-2 hours | Low | 1-3 |
| 5 | `Log.progress.out` runtime section | 1-3 hours | Low-Medium | 4 |
| 6 | Conditional optional-output sections | 1-3 hours | Low | 4 |
| 7 | Artifact/details links | 1 hour | Low | 4 |

## Success criteria

- The report explains both **what happened** and **how the run was configured**
- Users can diagnose low-quality or unexpected STARsolo output without opening multiple raw files
- Optional STAR outputs are summarized only when relevant
- The exporter remains self-contained, lightweight, and resilient to missing files
