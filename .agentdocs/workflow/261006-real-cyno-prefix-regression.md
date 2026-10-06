# Real Cyno prefix engineering regression

Date: 2026-10-06. Status: REAL_PREFIX_EXECUTED; compression equivalence passed,
algorithm change localized to pruning; initial unresolved checks below are historical.
Follow-up independent correctness qualification and the additional clipping repair
are recorded in [CPU stability / GPU entry](261006-cpu-stability-gpu-entry.md).
Preceding implementation was tested and pushed as `bbdd105` to origin/main.
The pre-existing ParametersSolo.cpp change remains uncommitted and preserved.

## Input and sampling

The user supplied the Cyno raw-data share. Selected one unmodified matched library:
`TMJ-condyle-1_S1_L001_R1_001.fastq.gz` and corresponding R2. The two source gzip
files are 8,970,977,352 and 19,888,346,316 bytes. Extraction is streaming, not a
full download/decompression: first 1,000,000 synchronized pairs, preserving headers,
sequence and quality lines. Every pair ID and sequence/quality length was checked;
source size and modification time were unchanged across extraction.

Local fixture: `data/fixtures/tmj-condyle-1-prefix-1m-20261006` (Git ignored).
Manifest includes source metadata, selection method, output hashes and observed
read lengths. R1: 28 bp; R2: 91 bp. This supports the engineering 16+12 barcode/UMI
layout, but no experimental-group assignment or biological representativeness is inferred.

Output sizes: R1 128,416,252 bytes; R2 254,416,252 bytes (~365 MiB combined).
Output SHA256:

- R1: `6d1c61b73e8b47a030402557a0b64bf63a124406c2540abfe7884d07e14d8087`
- R2: `fd4ac0be9c455601fa7ae8e918924575ab2090cc91fccd91fb16a85359617ebd`

The prefix is intentionally cheap and reproducible, not random/cell-stratified;
do not use its cell calling/composition as full-library biological results. Entire
source-file gzip CRC and full-source checksum were not checked; output hashes are verified.

## Execution and gates

`scripts/subset_fastq_pairs.py` rejects malformed/truncated pairs, mismatched IDs,
sequence/quality length mismatch, insufficient reads, changing source metadata and
existing output directories. Partial failures cannot produce a COMPLETE manifest.
Its two regression methods passed, covering prefix/header preservation, non-overwrite,
truncation, pair mismatch and bad qualities.
The benchmark harness also passed a regression method exercising identical outputs,
changed Velocity counts (exit 2), missing required files, and corrupt fixture hashes.

`scripts/benchmark_cpu_subset.py` runs builds sequentially on the same hashed fixture:
frozen pre-fix binary, final zlib, final libdeflate. Eight threads, default non-legacy
algorithms, Gene/GeneFull_Ex50pAS/Velocyto, EM, EmptyDrops_CR, unsorted BAM with raw
CR/UR tags. Required raw integer matrices, feature/barcode axes and SJ are hashed
and compared. `/usr/bin/time -v` records CPU, wall time and RSS. One run per binary
is an initial screen; cache/order is confounded, not evidence of a speedup.

First attempted invocation included CB/UB with unsorted BAM. STAR correctly rejected
that incompatible configuration before loading data. The corrected profile requests
CR/UR only; corrected attempts are isolated, not overwrites of the failed evidence.

Corrected run evidence: `/home/ubuntu/star-cross-validation/real-tmj-prefix-1m-20261006-r2`.
The runner was subsequently hardened to persist a source snapshot for future runs,
reject a single-build comparison, and return nonzero on artifact differences. Those
late harness-only changes do not retroactively bind this already-running invocation.

Baseline completed in 187.34 seconds; final zlib 141.72 seconds; final libdeflate
145.31 seconds. All processed 1,000,000 input pairs. Mapping was approximately
6-7 seconds; index load and junction insertion dominate this subset. One-pass times
are confounded by cache/order; the diagnostic ablation compilation also overlapped
part of the libdeflate run. No speedup is established and default compression remains OFF.
Baseline summary: valid barcodes 95.7035%; genome mapping (unique+multiple) 94.1593%;
unique genome mapping 81.2807%; gene mapping (unique+multiple gene) 39.7205%.
These check the engineering input profile, not cellular composition or treatment effects.
Native perf sampling is unavailable in the current WSL installation; time/RSS and
stage timestamps are measurements, not a sampled function-level hotspot profile.

## Inspected comparisons and ablation

- Final zlib vs libdeflate: all five raw integer matrices have zero changed
  coordinates. Their sorted BAM records are exactly equal: 1,254,349 records each.
  The comparison streams gzip/BGZF members; the prior tiny-fixture helper used
  gzip.decompress, whose concatenated-member suffix copies were too expensive on
  real BAM. That comparator was replaced with a streamed reader and re-tested.
- Before vs final: unique reads 812,807 -> 809,683; multi-locus reads
  128,786 -> 131,660. This is a real alignment/count change, not row ordering.

| Raw artifact | Changed coordinates | Before sum | Final sum | Absolute count delta |
|---|---:|---:|---:|---:|
| Gene | 989 | 177855 | 178004 | 1181 |
| GeneFull_Ex50pAS | 1673 | 303724 | 303726 | 1856 |
| spliced | 581 | 137414 | 137211 | 649 |
| unspliced | 971 | 116840 | 116519 | 1021 |
| ambiguous | 74 | 12821 | 12787 | 74 |

- Diagnostic ablation: exported committed source bbdd105 into an isolated WSL
  directory, restored ONLY the removed pruning branch, built with zlib. Its binary
  SHA256 is `978df99a3c00578027b48e3e6c8d9f87974a789799202aee31ab8b6ad39954f7`.
  This export omits the unrelated local ParametersSolo.cpp addition, but the run
  explicitly requests Gene with Velocyto, so its automatic-Gene insertion cannot
  change this profile. Main worktree source and production binaries were not changed.
- Ablation run (142.04 seconds) restored 812,807 unique reads and matched the old
  baseline SJ plus EVERY declared raw matrix and axis hash. It localizes this
  fixture's raw-artifact differences to the old pruning branch; it is not an
  independent correctness proof or an endorsement to restore the unsafe bound.
- Evidence: `real-tmj-prefix-1m-20261006-r2/result.json`,
  `before-vs-zlib-count-deltas.json`, `zlib-vs-deflate-count-deltas.json`,
  `zlib-vs-deflate-bam-records.json`; diagnostic evidence in
  `/home/ubuntu/star-cross-validation/real-tmj-pruning-ablation-20261006` includes
  actual runner snapshot, argument comparison and reference-output signature checks.

The initial three-build run is DIFFERENCES_REQUIRE_REVIEW, not a general PASS.
No old baseline was overwritten to hide the difference. Next correctness gate:
examine changed reads and compare equivalent scoring/search semantics against an
independent no-prune oracle before broader algorithm/release qualification.

## Boundaries and next actions

Inspect exact raw comparisons and BAM semantic records before claiming qualification.
If differences occur, investigate them; do not regenerate a reference to force PASS.
No full-library, filtered-cell, legacy-mode, cross-platform or GPU qualification is
claimed. Representative throughput comparisons need warmups, repeated/interleaved
runs and larger sampling after this engineering gate passes.
