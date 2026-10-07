# GPU Acceleration: Implementation and Decision Plan

Status: opt-in CUDA SA-remap and isolated resident seed-search experiments;
production search integration and end-to-end promotion remain unqualified.

Current detailed execution sequence: [CPU/GPU optimization plan](../../docs/OPTIMIZATION_PLAN.md).
It incorporates the executed native and official timelines; this roadmap remains
the broader candidate inventory and the companion validation contract remains authoritative.

Latest execution: [native Windows and resident seed experiment](261006-native-windows-resident-seed-experiment.md).
Native CPU/CUDA builds are verified; seed-search local throughput is promising,
but startup amortization and production read scheduling remain separate gates.
Reviewed: 2026-10-06. Source inspected: `1f9aba49acad69ead85cc938ccbe2c4785ed62d7`.
A pre-existing `source/ParametersSolo.cpp` change is also present; capture its diff
in any baseline. A version string alone does not identify the code tested.
Suggested implementation branch: `codex/gpu-accel`; this document does not create it.

Companion: [validation and benchmark contract](261006-gpu-validation-and-benchmark-plan.md).
CPU prerequisites: [performance plan](261006-performance-optimization-and-upstream-sync-plan.md).

## 1. Objective and corrected assumptions

Accelerate supported STAR-Cross workloads while preserving the selected CPU
algorithm's observable results. Choose offload targets from measured end-to-end
cost, batching opportunity and semantic complexity. There is no promised speedup.

| Previous assumption | Corrected decision |
|---|---|
| Fixed kernel gains and 5x-8x overall speedup | Hypotheses requiring local measurements; vendor results do not predict this workload |
| BGZF immediately improves runtime 15%-25% | Only BAM-producing workloads benefit; compare with optimized CPU compression first |
| STAR stitching is generic Smith-Waterman/WFA | Preserve specialized seed, splice, overlap, fragment and selection rules |
| A deterministic coordinate tie-break is equivalent | Preserve CPU traversal, randomization and primary-selection semantics for each mode |
| All UMI modes can use one Union-Find rule | Exact, graph, CR and Directional modes require separate contracts |
| A fixed 32GB SA determines GPU support | Measure Genome, SA, SAindex, junctions, scratch, queues and runtime overhead |
| Every GPU failure permits transparent fallback | Replay only uncommitted work; fatal context/output failures abort without publishing success |
| Successful smoke exit proves equivalence | Require declared artifacts, coverage and comparators from the companion contract |

The [historical experiment](done/260412-windows-port.md) records 98K UMIs over
774 launches (about 127 UMIs/launch), overhead exceeding gains, and mapping at
about 88% of runtime. These are historical observations, not a new measurement
or proof that all GPU approaches fail. Reproduce the workload where possible.
At that historical fraction, eliminating all non-mapping work caps speedup near
1.14x. New experiments must explain what changes the batching/cost balance.

## 2. Source boundaries

| Component | Source entry points | Consequence |
|---|---|---|
| Scheduling/mapping | `source/ReadAlignChunk_processChunks.cpp`, `source/ReadAlign_mapOneRead.cpp` | Keep read identity and measure queue waits/exclusive work |
| Seeding/index | `source/ReadAlign_maxMappableLength2strands.cpp`, `source/Genome_genomeLoad.cpp`, `source/PackedArray.cpp` | Verify actual search/index representation before assigning threads or warps |
| Stitching | `source/stitchWindowAligns.cpp`, `source/stitchAlignToTranscript.cpp` | Preserve candidate selection and specialized splice handling |
| Primary selection | `source/ReadAlign_multMapSelect.cpp`, `source/ReadAlign_quantTranscriptome.cpp` | Preserve mode-specific order, RNG consumption, primary flags and truncation |
| UMI correction | `source/SoloFeature_collapseUMIall.cpp`, `source/SoloFeature_collapseUMI_Graph.cpp` | Preserve thresholds, groups, multigene filtering and corrected UB tags |
| Sorted BAM | `source/BAMbinSortByCoordinate.cpp`, `source/BAMbinSortUnmapped.cpp`, `source/bamSortByCoordinate.cpp` | Compression is outside the unsorted BAMoutput path too |
| BGZF | `source/htslib/bgzf.c`, `source/htslib/htslib/bgzf.h` | Reuse framing/writer logic instead of duplicating it per caller |
| EmptyDrops | `source/SoloFeature_emptyDrops_CR.cpp` | Per-count simulations, sorting and lower_bound already exist |

## 3. Phase 0: establish the evidence

1. Pin CPU source/diff, binary, compiler flags, dependencies, input/index/GTF/whitelist
   hashes and all effective arguments. Keep default and legacy baselines distinct.
2. Run existing CTest and smoke entry points with actual test discovery/counts;
   record missing tests. Implement the companion comparator before GPU qualification.
3. Profile matrix-only, sorted-BAM and Velocity workloads: loading, FASTQ decode,
   mapping, sorting, counting, filtering and final flush. Report CPU/thread time
   separately from wall time; nested/overlapping timers cannot be summed as percentages.
4. Record batch-size distributions, allocation and queue costs. Measure instrumentation
   overhead against an uninstrumented run; avoid per-base timing in hot loops.
5. Measure free VRAM, actual resident index bytes, peak device/host/pinned memory,
   transfer throughput and configurable headroom. Reprofile after CPU optimizations.
6. Produce profile.json, benchmark_runs.tsv and a module-selection decision using
   the estimate `1 / ((1-f) + f/s + overhead_fraction)`. Include all offload costs.

### External Parabricks benchmark

The official 4.7.1-1 linux/amd64 OCI image was downloaded on 2026-10-06; its
selected platform manifest is
`sha256:a748d86cbb850641a1e0afae6de2e7422f1375e4a0cce08a5c2cead9fa302237`.
Local OCI location: WSL `/home/ubuntu/container-images/clara-parabricks`.
Download/blob hashes were checked. Docker was absent at initial inspection;
subsequent user-authorized setup and scoped RNA runs are now complete. Keep
images out of Git. See [executed benchmark](261006-parabricks-benchmark-results.md).

The [runtime preflight](261006-parabricks-runtime-preflight.md) records the initial
runtime blocker and subsequent user-authorized Docker/NVIDIA setup. The original
index was rejected without metadata edits; an isolated STAR 2.7.2a-compatible
index was rebuilt and its Genome is byte-identical to the original. Three matched
1M-read RNA CPU/GPU rounds completed: CPU median 21.985s, fixed GPU median
50.937s (no speedup). Auto and auto+GPU-sort/write single runs also completed.
All nine 1M outputs, including the contextual STAR-Cross CPU run, have identical
complete normalized BAM record multisets and SJ tables. No STARsolo/Velocity or
large-library performance qualification follows from this result.

For future profiles, verify compatible container runtime/GPU integration and record GPU
visibility, `pbrun --version` and `pbrun rna_fq2bam --help`. Host nvidia-smi does
not prove container readiness. Official RNA docs [S1] declare STAR 2.7.2a compatibility
and a TranscriptomeSAM primary-selection caveat; this is not the CPU oracle for
our newer baseline. Confirm behavior against the pinned container's actual help.

Documented soloFeatures lists Gene, SJ and GeneFull. GeneFull_Ex50pAS and Velocyto
remain unsupported/unverified for this comparator until demonstrated. Use identical
biological reads/reference and matched task semantics; build compatible indexes
separately when necessary, retaining commands/hashes. Confirm cDNA/barcode input roles.
Use `--no-markdups` for a STAR-only comparison, or include equivalent CPU duplicate
marking and its time. Measure base RNA, gpuwrite, gpusort and both options separately
where supported. Do not compare matrix-only CPU timing against GPU BAM/sort/markdup.

Record WSL/native Linux and filesystem placement. Use the same storage class for
timed runs. Do not infer native Linux performance or GDS support from a WSL result.

## 4. Minimal architecture

The general architecture below remains a proposal. The first bounded adapter
implements `STAR_ENABLE_CUDA` and the module-specific `--gpuSjdbRemap` modes;
it does not implement the proposed generic `--gpuMode` or an asynchronous queue.
See [executed remap experiment](261006-gpu-sjdb-remap-experiment.md).

- `STAR_ENABLE_CUDA=OFF` by default. CPU configuration must not require CUDA/nvCOMP.
  Start with Linux/WSL CUDA; preserve Windows/macOS CPU builds.
- `--gpuMode off|auto|required`: off selects the oracle path; auto permits logged
  supported fallback; required fails preflight if requested capability is unavailable.
- Implement one adapter, bounded reusable buffers and completion/status handling.
  Avoid a generic GPU framework or four placeholder modules before proving one useful.
- Assign stable batch IDs; retain immutable inputs until exactly-once commitment.
  Bound queue depth and batch bytes. Preserve required logical output order.
- Start synchronously, then two buffers; add streams only after measuring benefit.
  Time packing, copying, result gathering, waiting and writing.
- Preflight OOM/unsupported shapes may select CPU. Mid-run replay requires a healthy
  context, retained inputs and no committed output. Illegal memory access, corruption
  or partial-write failure aborts with incomplete status and preserved diagnostics.
  Correctness failures never silently become successful CPU fallback.
- Record backend, fallback counts/reasons, rejected shapes, memory peaks and completion.

## 5. Candidate modules and gates

These are candidates selected after profiling, not a mandatory four-stage sequence.

### A. Standard BGZF compression

First compare zlib with a pinned libdeflate CPU build and measure BAM compression's
wall-time share. This project's CRAM output transcodes BAM separately; do not assume
the same benefit for CRAM.

Prototype on captured immutable uncompressed BAM blocks. Use nvCOMP standard raw
Deflate with pinned API/version [S2], not GDeflate. Prefer to keep HTSlib responsible
for framing, ordered writes and index offsets. Cover sorted/unsorted, unmapped and
transcriptome paths, including flush and close.

Bundled headers define `BGZF_BLOCK_SIZE=0xff00` and `BGZF_MAX_BLOCK_SIZE=0x10000`.
Check the selected compressor's worst-case bound including header/footer. Handle
expansion with a tested smaller-block or stored-block path. Verify BC/BSIZE, CRC32,
ISIZE, EOF, short writes and records spanning blocks. New block boundaries require
new virtual offsets/indexes.

Exit: identical decompressed bytes for identical block inputs, valid indexed retrieval
and full BAM semantics end-to-end, plus wall-time gain including transfers.

### B. Batched UMI neighbor generation

Capture real per-(cell, feature/gene) group distributions. Batch independent groups
in segmented buffers; never merge across groups. Sweep batch sizes to address the
historical tiny-launch failure; CPU handles tiny/oversized groups according to an
explicit threshold measured on the target device.

Initially offload Hamming-neighbor generation only and preserve CPU correction and
emission order. Consider sorting/correction later only if still a measured bottleneck.
Exact, graph/1MM, CR and Directional contracts remain separate. Directional preserves
its read-count inequality and ordered parent selection; numerical-root Union-Find
is not equivalent. Preserve multigene filtering and corrected UB tags. Barcode
correction and EmptyDrops are independent later candidates.

Exit: exact supported-domain neighbors, corrected identities, integer matrices and
cell calls; positive end-to-end gain over the same CPU mode.

### C. Bounded splice/extension subproblem

Measure call shapes/branches, then select one pure subproblem with explicit inputs,
outputs and CPU rejection codes. Keep candidate enumeration, pruning, junction
annotations, fragment handling and selection on CPU. Transfer explicit semantic
fields/read-only windows, never raw Transcript memory containing host pointers.

Evaluate WFA/GASAL2 only where scoring and endpoint semantics match the subproblem.
Whole STAR stitching is not ordinary affine DP. Preserve score ranges, mismatch
limits, motifs, overhangs, junction shifts, overlaps and clipping. DPX depends on a
suitable recurrence, ranges and device capability; Tensor Core gains are not assumed.

Exit: exact field-level results/decisions on supported cases, CPU coverage elsewhere,
and benefit after window gathering/transfers.

### D. Resident suffix-array search

On the available 96GB device, first test unchanged full-index representation if
measured memory permits. Preserve packed loads, sentinel/N symbols, strands, prefix
limits, suffix intervals and junction-index behavior. Compare per-read and cooperative
warp designs experimentally.

Two-pass index changes require synchronization and a new device index generation.
Use CPU seeding if memory is insufficient. UVM oversubscription and chromosome tiling
are separate experiments requiring fault/transfer measurement and complete candidate
enumeration across boundaries. Two-bit packing must retain ambiguous/sentinel symbols.
Sparse indexing must preserve semantics or be labeled an algorithm change.

Exit: exact seed intervals/lengths and downstream decisions, bounded memory, benefit
including upload for both single-job and reused-index scenarios.

## 6. Work packages and stopping rules

2026-10-06 executed CPU entry evidence is in
[CPU stability / GPU entry](261006-cpu-stability-gpu-entry.md): the corrected
1M-pair default profile agrees with pinned upstream and repeats exactly. A separate
single-thread profile identifies junction preparation as the largest sampled
self-time hotspot and seed/SA comparison as a mapping hotspot. The first executed
experiment targets the packed SA-remap in `sjdbBuildIndex`, with an optimized CPU
identity-copy baseline. Candidate D is the next bounded mapping experiment if
this index-preparation offload cannot beat that optimized baseline.
This subset is dominated end-to-end by index loading/preparation, and no GPU gain
is established. W0's full-library/platform coverage and W1's future kernel-field
fixtures remain distinct from the qualified engineering CPU oracle. CUDA 13.0.88
was found at `/usr/local/cuda` (absent from PATH), and the adapter runs on the local
Blackwell device. W2 runtime and scoped 1M RNA equivalence are verified; no
end-to-end speedup was observed. W2 STARsolo/Velocity and large-library coverage
remain unverified. See the executed external benchmark before repeating setup.

| Package | Deliverable | Required evidence |
|---|---|---|
| W0: baseline | manifests, fixtures, CPU repeatability | Complete artifacts and explicit chemistry/default/legacy |
| W1: comparator/profiling | corruption tests, timing and size distributions | Known errors detected; instrumentation overhead measured |
| W2: external benchmark | pinned Parabricks run report | Matched tasks, verified capabilities and version caveats |
| W3: one synchronous module | focused adapter and CPU oracle tests | Exact supported-domain behavior, bounded resources |
| W4: batching/overlap | queue instrumentation and failure tests | Exactly-once commitment, transfer costs included |
| W5: qualification | full report and support matrix | Companion correctness/performance gates pass |

W0/W1 do not depend on W2 runtime readiness. W3 depends on W1's module selection.
Defer a candidate if optimized CPU removes its benefit, memory is unsuitable or
equivalence cannot be preserved. Microbenchmark gain alone does not justify default
activation. Unsupported modes retain explicit CPU/rejection behavior and coverage.

## 7. Evidence sources

Official sources checked 2026-10-06; pin actual dependency versions before coding:

- [S1: Parabricks RNA](https://docs.nvidia.com/clara/parabricks/tool-reference/tools/rna_fq2bam)
- [S2: nvCOMP formats](https://docs.nvidia.com/cuda/nvcomp/) and [API](https://docs.nvidia.com/cuda/nvcomp/c_api.html)
- [S3: CUDA on WSL](https://docs.nvidia.com/cuda/wsl-user-guide/index.html)
- [S4: SAM/BAM/BGZF specification](https://samtools.github.io/hts-specs/SAMv1.pdf)

Local code and historical reports do not establish new performance results.
Proposed APIs, engineering thresholds and measured observations must be distinguishable.
