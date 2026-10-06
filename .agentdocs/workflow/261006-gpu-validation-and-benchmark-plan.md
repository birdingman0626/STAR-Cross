# GPU Validation and Benchmark Contract

Status: implementation specification; GPU kernels and the proposed harness are not implemented.
Reviewed: 2026-10-06. Companion: [implementation plan](261006-gpu-acceleration-research-and-roadmap.md).
This is the authoritative GPU acceptance contract. The historical 21-file CPU
comparison remains a fixture check, not proof of GPU or Velocity coverage.

## 1. Baselines and claims

Use identical STAR-Cross source snapshots/effective parameters for CPU and GPU
equivalence tests. Capture dirty diffs, including the current ParametersSolo.cpp
change, and binary hashes. Keep default and legacy baselines separate. Repeat CPU
runs first to identify ordering/RNG or floating-point variability. Record thread
count and read-to-thread/RNG assignment: a seed alone does not fix RNG consumption.

Three comparisons have different purposes:

1. Same-baseline acceleration: exact scientific results for supported modes.
2. Parabricks: matched-task external performance/behavior, with version/feature
   differences recorded; not the authoritative oracle for this port.
3. Changed algorithms: separately named experiments with disagreement analysis;
   they cannot pass the acceleration gate by relaxing its tolerances.

## 2. Run manifest and required coverage

Retain source commit/diff hash; binary/container manifest digest; compiler flags and
dependencies; OS/WSL/kernel, CPU/thread resources, RAM, GPU/free VRAM, driver/toolkit;
input/index/GTF/whitelist hashes; complete effective arguments, chemistry/read roles
and seed; storage/cache conditions; exit/completion status; backend/fallback coverage;
timings, memory peaks and output hashes. Use unique output directories and immutable
references. Never automatically regenerate a baseline after a comparison failure.
Local manifests may contain paths; redact them in externally shared reports.

Declare required artifacts before execution. Use PASS, FAIL, SKIPPED, UNVERIFIED and
NOT_APPLICABLE with reasons. Missing required inputs/outputs, unexecuted tests or an
empty reference block qualification. Legitimately empty matrices still require axes,
dimensions and a completed producer record. No-GPU CI proves CPU behavior only.

## 3. Workload coverage

| Profile | Purpose | Required results |
|---|---|---|
| Synthetic tiny reference | Boundaries and deterministic debugging | Expected seeds, decisions, alignments and counts |
| Bulk RNA, sorted BAM | Mapping/compression | Full BAM semantics, index queries, junctions and mapping statistics |
| Bulk RNA, unsorted BAM | Writer/flush integration | Complete record multiset and output |
| Droplet, matrix-only | Counting benefit without BAM cost | Gene/full-gene integer matrices, axes and feature statistics |
| Droplet, Velocity | Production velocity compatibility | Gene and all three velocity layers with consistent axes |
| Deep representative scRNA | Cell calling/tail memory | Filtered barcode identities/matrices, EmptyDrops and EM |
| Advertised optional modes | Two-pass, transcriptome, multimappers, clipping, chimeric output | Mode-specific artifacts and fallback coverage |

For compatible Velocity inputs, explicitly request
`--soloFeatures Gene GeneFull_Ex50pAS Velocyto`. Resolve paths from the version and
feature configuration. Require spliced.mtx, unspliced.mtx and ambiguous.mtx, with
validated feature/barcode axes. Do not assume Gene equals the sum of velocity layers
without a proven counting contract. Native CPU vs native GPU is the primary test;
comparison with a BAM-extraction method requires its own counting specification.

Subsample pairs together and check matching read IDs. Historical scripts pass R2
then R1 to STAR; verify read lengths, chemistry and whitelist to establish cDNA and
barcode roles rather than guessing from filenames. Small subsets do not qualify
cell calling. Add full-data cell-filter tests with sufficient droplet depth.

## 4. Comparator specification

Proposed entry point: `scripts/validate_gpu_equivalence.py` (not yet implemented).
Suggested interface: `--reference-run`, `--candidate-run`, `--contract`, `--output-dir`.
Implement the comparator before GPU qualification. Its versioned contract declares
required artifacts, baseline identity, normalization and numerical tolerances.

| Surface | Required comparison | Failure rule |
|---|---|---|
| Input | Exact read/pair counts and identities | Lost/duplicated reads or mate mispairing fails |
| Kernel/candidate results | Exact initialized semantic fields, candidate coverage and relevant order | Any unexplained mismatch fails; exclude padding/pointers |
| Compression-only | Exact decompressed BAM bytes for the same input stream | Changed bytes, CRC/EOF/truncation errors fail |
| End-to-end BAM | Exact normalized record multiset with multiplicity | Compare flags, coordinates, MAPQ, CIGAR, mates, TLEN, SEQ/QUAL and scientific tags |
| BAM header/index | SQ/RG semantics, declared sort order, indexed retrieval | Invalid order/index or missing records fails |
| SJ.out.tab | Exact sorted full rows/counts for all junction classes | Any unexplained difference fails |
| Raw integer matrices | Exact dimensions, axes, sparsity support and values | Missing/extra coordinates or changed integers fail |
| Cell calls/filtered matrices | Exact selected barcode identities and integer values | Changed call fails regardless of gene correlation |
| Velocity | Exact three matrices, valid mutually consistent axes | Missing layer, shifted axis or changed category/count fails |
| Integer statistics | Parse stable scientific fields from logs/statistics | Unexplained change fails; timing fields excluded |
| Floating results | Predeclared finite-value/algorithm-specific contract | Unset tolerance is UNVERIFIED; NaN never silently passes |

BAM comparison may reorder records/tags, but must preserve repeated-QNAME
multiplicity, primary identity, tag types/values and all scientific fields. QNAME
alone is not a unique key. Allow only named metadata exclusions such as PG command
paths/timestamps, recording each. Validate original sort order separately so sorting
for comparison does not conceal invalid output. Compression unit tests use identical
input bytes and need no header normalization. BAM binary bytes are not SAM text.

Parse Matrix Market dimensions and coordinate ranges, validate axis IDs, then align
by explicit feature/barcode identity. Report axis-order differences separately;
strict delivery profiles also require the documented output order. Declare how
explicit zero and duplicate coordinates are handled; do not silently normalize away
malformed data. Compare all supported integer feature types, not just Gene.

Initially retain CPU EM/EmptyDrops and their operation order. If offloaded later,
establish CPU repeatability and predeclare justified `atol + rtol * abs(reference)`
bounds, handling zero/near-zero values, totals, convergence, iteration counts and
downstream decisions. Kahan summation alone does not establish equality. Cross-platform
floating differences require an explicit contract, not a universal percentage waiver.

Comparator self-tests must inject missing reference/candidate files, an empty
reference, stale manifests, truncated BAM, lost secondary records, duplicated records,
changed MAPQ/tags/primary flags, a rare-junction change, unmatched axis permutation,
one changed integer, missing velocity layers, NaN and a changed cell call. Each case
must fail with useful example IDs and a nonzero status.

## 5. Validation stages

### T0: CPU baseline and actual test discovery

Reuse existing doctest/CTest infrastructure in test/CMakeLists.txt. Record discovery
and executed test counts. validate_build.sh currently uses `ctest -R star_tests`,
which may not match doctest-discovered case names; zero executed tests is not a pass.
Use discovered cases or add explicit labels when implementing the harness.

For a newly created CPU build directory, these are existing commands, not proposed
GPU flags (run from the repository root and use a fresh directory per baseline):

```bash
CPU_BUILD_DIR=$(mktemp -d)
cmake -S source -B "$CPU_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON
cmake --build "$CPU_BUILD_DIR" --target STAR star_tests --parallel 4
ctest --test-dir "$CPU_BUILD_DIR" -N
```

Select and run the discovered STAR doctest cases with `ctest --test-dir` and
`--output-on-failure`, recording the selection/count. Do not include an unbuilt
third-party test target by accident or silently exclude failed STAR cases. Preserve
the directory and binary hash with the manifest. CUDA build commands are added only
after the proposed build option actually exists.

### T1: real CPU oracle plus hand-checkable fixtures

- BGZF: empty/maximum blocks, incompressible data, record boundaries, expansion,
  CRC/EOF, flush/close, short writes and random-access indexes.
- UMI: empty/single groups, multiple cells/genes, equal-count ties, Hamming-1 chains,
  Directional threshold boundaries, CR behavior, multigene filtering and UB tags.
- Alignment: ambiguous bases, both strands, annotated/novel canonical/noncanonical
  junctions, overlaps/indels, clipping, paired fragments, score ties, rejection codes
  and mismatch/exon limits; compare semantic fields instead of raw struct memory.
- Seeding: packed-word edges, N/sentinels, both strands, repetitive intervals,
  truncated prefixes and junction-augmented indexes.

Call actual production CPU functions as oracles. Reimplementing the proposed GPU
algorithm in the tests does not provide an independent correctness check.

### T2: batch, concurrency and recovery

Capture deterministic chunks with their required state; execute independent CPU/GPU
copies without shared counters/RNG or duplicate output writes. Sweep empty, tiny,
tail, oversized and mixed-shape batches, streams and completion orders. Require exact
candidate coverage and relevant order, not 99.99% agreement.

Inject allocation failures, unsupported devices/shapes, queue saturation, kernel
faults, partial writes and replay attempts. Verify bounded memory, exactly-once
commitment, permitted logged CPU fallback and nonzero exit/incomplete status for
fatal failures. Run CUDA memory/race diagnostics on implemented kernels.

### T3: end-to-end regression

Run synthetic, paired ~1M smoke and representative full workloads. The historical
434M dataset is one case, not universal coverage. Include default/legacy, relevant
UMI modes, thread/batch settings and GPU off/auto/required once implemented. Two-pass
must exercise index mutation; transcriptome tests must cover RNG/primary selection.
Unsupported modes must use the documented CPU path or fail before successful output.

### T4: downstream sensitivity when scientific inputs differ

Identical validated matrices do not need a complete DE rerun to prove matrix equality.
For separately named algorithm experiments with changed outputs, use a fixed
sample-level analysis and report rare-gene/junction discrepancies, effect-size/ranking
changes and threshold sensitivity. High correlation cannot excuse count errors;
zero FDR-threshold crossings is not a universal acceptance criterion.

## 6. Performance and promotion gates

Compare optimized CPU and GPU on the same task, inputs, storage, output/compression
policy and declared CPU resource budget. Report matched-CPU and best-practical CPU
configurations separately if helper threads change resource use. Exclude validation
from timing, but include startup, loading/upload, packing/copies, final flush and all
required outputs. Separate one-job and amortized index-reuse results.

Prefer at least three paired measured runs after a separately reported warm-up.
Alternate run order and record cold/warm cache conditions; avoid system-wide cache
flushes on shared hosts. Unrepeated full runs remain provisional. Report every run,
median/range, speedup, declared reads-or-pairs throughput, CPU/host/device/pinned
memory and fallback counts. Do not sum overlapping timers or relabel kernel speed
as end-to-end speed.

Initial engineering selection proposal: >=10% median end-to-end time reduction on
the target profile with improvement in each measured pair, all mandatory correctness
checks passed, and no unexplained >5% median slowdown on other supported profiles.
These are project thresholds, not biological/vendor claims. Change them explicitly
before evaluating results. If noise obscures benefit, collect more measurements.
For unaffected workloads, such as matrix-only with BGZF offload, require no material
overhead rather than a speed gain. One 96GB GPU does not qualify all device tiers.

## 7. Qualification checklist

- [ ] Frozen inputs/baseline, required-profile manifest and CPU repeatability.
- [ ] Comparator corruption tests detect all specified failures.
- [ ] Actual CPU test discovery and execution recorded.
- [ ] Module, concurrency, recovery and memory diagnostics pass.
- [ ] BAM, junction, matrix, cell-call and Velocity checks cover advertised modes.
- [ ] Numerical tolerances, if used, were justified before comparison.
- [ ] Performance includes all costs and resource/fallback accounting.
- [ ] Relevant Linux/Windows/macOS/endian CPU tests pass; CUDA absence does not
  break CPU configuration/linking. CUDA support is scoped to tested platforms.
- [ ] Report states PASS/FAIL, missing evidence and unsupported modes separately.

Retain contract version, coverage, mismatch examples, timings, peak memory, manifests
and reproduction commands. Writing this plan or downloading an image completes none
of the execution/qualification checkboxes above.
