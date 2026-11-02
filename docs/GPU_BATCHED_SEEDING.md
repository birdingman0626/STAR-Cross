# Adaptive batched seeding experiment

Branch: `codex/gpu-batched-seeding`, based on main `1367fb6`.
The default remains `--seedSearchBackend off`. Vulkan is deferred until this
architecture meets the compute-speed target and its resource costs are acceptable. This is an experimental
read-mapping path, separate from `--gpuSjdbRemap` (junction-index preparation).

## Acceptance target

The owner's target is at least **3x faster for the complete computation**, with
loading and export excluded. An 80-second CPU computation must therefore take
at most 26.67 seconds on the GPU path; 10 seconds would be an 8x improvement.
This is an adoption criterion, not a promised property of GPU execution.

The comparison must use the same inputs, parameters, CPU resources and scientific
output contract. Define and freeze the measured computation before tuning:

- Exclude file loading/decompression, reference construction/loading, one-time
  device-index initialization/upload, and final file serialization/export. Report
  their costs separately, together with whole-job time and the workload size.
- Include read preparation after loading, adaptive seeding, hit expansion,
  window formation, stitching, scoring and selection. For a STARsolo workload,
  include barcode/UMI/counting computations rather than treating all Solo work
  as export. Preserve a separately named alignment-only breakdown.
- Include scheduling, repeated CPU/GPU transfers, synchronization and work
  needed to turn GPU results into the same final scientific records. Moving
  computation outside the timer is not an optimization.
- Time a bounded resident-input computation with all worker/device work joined
  at its boundaries, retaining results for validation outside the timer. Do not
  subtract summed worker durations or CUDA-event durations from whole-job time:
  those intervals overlap. Preserve all paired trials and report uncertainty;
  the lower speedup bound must reach 3x for performance acceptance.
- Report peak host RSS, process commit, VRAM and buffer limits. Do not accept a
  time improvement by silently changing alignment heuristics, dropping difficult
  reads, reducing candidate limits or allowing incomplete outputs.

**Current status: correctness passes for the tested scope; the complete-compute
3x target is not established.** The existing whole-job runner and isolated search
replay do not yet implement the resident complete-computation timing boundary.
Neither can be substituted for that acceptance measurement.

## Execution model

Each mapping worker prepares a bounded batch of clipped reads. The CPU control
uses lightweight C++20 coroutines to preserve each read's adaptive search state.
The current CUDA path executes quality splitting, prefix lookup, both search
directions, sparse-SA distances, supplementary seeds, adaptive continuation and
seed consolidation on the device. One thread owns a read; it consumes emissions
in original order and inserts accepted seeds into a sorted bounded device list.
Consolidation preserves multiplicity rejection, first-winner duplicate handling,
read-position/length ordering, and counters incremented before deduplication.
A batch returns complete per-read seed lists and summaries,
not a request/result exchange for every adaptive round. The former round-driven
CUDA binary is retained as an experimental comparison, not another CLI mode.

The CUDA executor also expands accepted seeds into SA hits and projects genome
coordinates, strand and oriented read start on the device. Windows reuse those
coordinates in both anchor creation and hit distribution. The CPU copies the
returned seed lists into the legacy piece table and runs splice-junction splitting,
window formation, stitching, selection, BAM and Solo code in worker input order. The
CUDA path no longer repeats `qualitySplit` or `storeAligns` on CPU. No
full `ReadAlign` instance is allocated per active read; alignment scratch remains
per worker. These remaining CPU stages are not described as GPU execution.

The CPU executor is an executable control for the scheduling change. The CUDA
executor uploads the effective Genome + packed SA + prefix index once, after junction insertion
and before mapping workers start. Each worker owns one nonblocking stream and
its bounded buffers. A read pool and descriptors upload once per batch. The
device returns arena usage, list heads and summaries, then only the occupied event payload;
both completions synchronize that stream, not the whole device. Workers can
overlap streams, but reads within one worker still commit synchronously. This
is neither cross-process index persistence nor overlap of every pipeline stage.

The isolated query-replay API still uses one thread per search. Production
adaptive CUDA now uses one thread per read, retaining dependent binary searches
inside it. Sparse indexes re-evaluate distance matches when emitting tied best
seeds, avoiding scratch allocation proportional to sparse D. The extra searches
are counted. Device ownership reduces host round trips; it does not guarantee
faster kernels or remove the algorithm's memory dependencies.

## Selection and supported scope

Build normally for CPU, or add `-DSTAR_ENABLE_CUDA=ON` and a suitable
`-DSTAR_CUDA_ARCHITECTURES=...` to the existing CMake invocation. Host C++20 and
the separately selected CUDA device language level remain independent.

| Parameter | Behavior |
| --- | --- |
| `--seedSearchBackend off` | Original synchronous path; default |
| `cpu` | Adaptive round scheduler, all searches on CPU |
| `required` | Complete adaptive seeding executes on CUDA; preflight/runtime failures fail the run |
| `auto` | CUDA when initialized and a batch has at least 256 reads; smaller batches use CPU; preflight fallback is logged |
| `--seedBatchSize N` | At most N reads per worker batch; default 4096, valid range 1–65536 |

The auto threshold is an experimental dispatch policy, not a measured portable
break-even point. Prefix shortcuts need no SA extension request even in `required`
mode. For the device scheduler, qualification requires actual GPU batches for
every nonempty worker and zero CPU rounds/queries; extension counts alone are
insufficient. Frozen legacy query executors still require GPU extension calls.

Initial support is fixed-index, one-pass mapping, including ordinary single/
paired reads and STARsolo CR4 clipping. Two-pass, BySJout, WASP, paired-overlap
remapping, SpliceGraph and chimeric mapping are rejected when batching is selected.
Random multimapper ordering requires a single logical worker. The original
`off` path retains support for these modes. Legacy seed-capture instrumentation
must be used on `off`; it does not count the new scheduler's searches.

## Memory, failure and accounting

Prepared inputs have a 4 MiB per-worker accumulation target (the final read may
cross it); the numeric read pool has a hard 4 MiB capacity. Each batch allows at
most 131072 seed events (accepted distinct seeds for CUDA; raw emissions for the
CPU control). Exceeding a bound fails explicitly and requests
a smaller batch; no candidates are silently dropped. Coroutines, split vectors,
strings and result vectors also consume host memory. Process peak RSS is the
physical-memory measure, rather than the buffer budget alone. Also report commit
and VRAM: low RSS growth does not mean CUDA has no host-memory commitment cost.

Device allocation is Genome + SA + prefix index + per-worker input/event/hit
capacity. Input descriptors and output staging are pinned; the whole index is
not pinned. The 131072-node event arena adds about 8 MiB of device memory and
8 MiB of pinned output staging per worker. Per-read summaries add 112 bytes in
each of device and pinned storage, about 448 KiB per worker at a 4096-read cap.
Capacity is checked before mapping.
Arena overflow, the existing per-read seed limit, zero search stride and non-progressing searches reject the
entire batch, with no partial output accepted. The query-replay API also checks
slot generations. There is no implicit runtime CPU retry after a CUDA failure.
Per-worker buffers outlive all worker threads.

The hit-expansion experiment has an additional 262144-hit cap per worker batch
(16 bytes per hit, 4 MiB device plus 4 MiB pinned staging), plus 2 MiB each of
device/pinned projection descriptors. Seeds remain resident while the CPU
schedules complete-read chunks within this cap. Host hit payloads are released
as each read is consumed, before scheduling the next chunk; the read batch is
not re-seeded. A single read exceeding the cap fails explicitly; alignments are
never truncated. `gpu_hit_batches` records these potentially smaller chunks.
This is a bounded intermediate bridge, not the final memory architecture: device
window construction should consume these hits directly and remove their bulk
download. `hit_expansion=device` and `gpu_projected_hits` expose actual execution.

`SEED_BATCH` lines record setup seconds, device bytes, pinned bytes, reads,
CPU rounds, largest active batch, actual CPU/GPU extension counts, GPU batch
launches and worker elapsed time. `scheduler=device_adaptive` identifies the
new device-owned schedule; `consolidation=device` identifies device-side sorting,
deduplication and counters. Hit-expansion worker times also include projection
scheduling/transfers; older seed-only worker times have a narrower scope.
Worker times overlap and must not be added to process
wall time. The isolated adapter's kernel timer uses CUDA events; whole-process
benchmarks include setup, scheduling, transfers, synchronization and cleanup.

## Validation and evidence

The following first-stage measurements belong to the frozen host-round/CUDA
experiment under ignored `data/validation/gpu-batched-seeding-20261101/`.
They are not performance measurements of the newer device-owned scheduler.
`candidate.json` and its source archive freeze source contents, binary hashes,
build options, dependencies and machine details. Each benchmark retains its
executed command, input/reference content hashes and scientific output receipts.
Failed attempts are retained, not replaced by the successful receipt.

- `profile/result.json`: current-main 100K read diagnostic capture. Sampled
  nested worker seed time is 50.85% of sampled mapping time, including 43.73%
  extension time; stitching is 47.62%. These are nested CPU intervals, not an
  additive whole-process wall-time breakdown. Capture I/O and index dumping
  are excluded from those intervals. Only two workers received this small
  fixture, so this profile does not characterize full eight-worker scaling.
- `baseline/result.json`: two original 1M-read full-contract runs agree.
- `cpu-mini-v4/result.json`, `cuda-mini/result.json`: differential comparisons
  against the frozen original binary across dense/sparse D=3 indexes, both
  strands, N/spacer boundaries, short/clipped reads, fixed supplementary seeds,
  paired reads and batch sizes 1/7/256. Complete BAM records/headers, SJ and
  scientific final fields agree. Negative checks cover unsupported modes,
  invalid sizes and missing devices. These fixtures are not performance tests.
- `star_gpu_seed_slots`: two concurrent slots, independent read generations,
  exact fields, empty batches, stale generations and capacity rejection.

- `cuda-ctest.log`: 127/127 tests passed, including the registered CPU-round
  contract. `default-regression.log` compares the modified default-off path
  against frozen main. `cuda-slot-memcheck.log`: Compute Sanitizer found 0 errors.
- `real-query-replay.json`: all 7974 captured real extension requests agree on
  length/lower/upper/multiplicity. Three warm replay rounds had median CPU/GPU
  times 9.39/1.29 ms (CPU replay uses two threads); index setup was 3.78 s.
  This local search improvement is not a whole-job speedup.

### 1M-read screening

`screen-1m/result.json`, eight workers, one run per configuration, native
MSVC 19.44 / CUDA 13.4 / RTX PRO 6000 Blackwell Max-Q, 4096-read batch cap:

| Path | Whole job (s) | Peak RSS (GiB) | Peak process commit (GiB) |
| --- | ---: | ---: | ---: |
| Frozen main CPU | 44.81 | 29.237 | 29.690 |
| CPU round scheduler | 44.95 | 29.303 | 29.770 |
| Required resident CUDA | 49.23 | 29.430 | 55.760 |

All three complete scientific contracts agree, including all Velocity layers.
CUDA executed 4904297 extension queries (zero CPU extensions), allocating
27220356785 device bytes (25.351 GiB) and 3 MiB pinned staging. Setup took 4.04 s.
RSS rose by about 198 MiB; process commit rose by about 26.1 GiB. The large commit
increase is observed on this Windows configuration, not a cross-platform claim
or a measured attribution to a particular driver allocation.

Main spent approximately 34.6 s before its mapping phase and 7.0 s mapping.
The corresponding CUDA mapping interval includes adapter setup and teardown;
the per-worker search timers exclude both. Do not subtract their summed values
from wall time to manufacture a speedup. The observed whole job was 9.9% slower.
This single ordered screening is confounded by cache/order/background load;
it does not establish a universal regression or a performance promotion.

### 5M-read paired screening

`paired-5m/result.json`: one warmup per version, then three alternating pairs,
eight CPU workers and the same full output contract. All eight runs, including
warmups, agree on the complete scientific outputs.

| Pair | Execution order | Original CPU (s) | CUDA (s) | Background CPU mean, original / CUDA |
| --- | --- | ---: | ---: | ---: |
| 1 | CPU, CUDA | 69.92 | 72.24 | 6.40% / 5.92% |
| 2 | CUDA, CPU | 78.15 | 72.69 | 26.01% / 7.44% |
| 3 | CUDA, CPU | 64.68 | 69.82 | 16.14% / 15.45% |

These are whole-job times, **not** the new compute-only acceptance measurement.
The median paired CUDA/CPU ratio is 1.033; the descriptive bootstrap interval is
[0.930, 1.080]. Three pairs with changing background load cannot establish a
speedup or a precise regression. The receipt remains `UNVERIFIED_ENVIRONMENT`;
the unfavorable pairs and the busy CPU run are all retained. Median paired peak
RSS growth is 218.7 MiB; CUDA peak commit is about 55.8 GiB versus 29.7 GiB for
CPU. Passing the runner's RSS check does not accept that separate commit cost.

### CUDA timeline attribution

`native-timeline/` and `native-timeline-5m/` retain Nsight Systems 2026.3.2
commands, reports, SQLite exports and hashed `attribution.json` files. Both
traced runs also pass complete BAM/SJ/Solo/scientific-statistic comparisons with
frozen main. They use CUDA tracing without CPU stack sampling; profiled timings
are attribution evidence, not performance acceptance.

| Fixture | Kernel launches | Active CUDA streams | Sum of kernel durations | Union of kernel intervals | First-to-last kernel span |
| --- | ---: | ---: | ---: | ---: | ---: |
| 100K reads | 456 | 2 | 0.0363 s | 0.0353 s | 1.412 s |
| 5M reads | 22548 | 8 | 2.984 s | 2.684 s | 23.675 s |

For 5M reads, at least one search kernel was executing during 11.34% of that
23.675-second span. This measures timeline coverage, **not SM occupancy**, and
the gaps include host computation, I/O and scheduling. It confirms substantial
room outside active GPU searches; it does not identify every gap as stitching.
CPU stack/stage attribution is still needed before choosing the next kernel.

The 5M capture transfers the 27.184 GB effective index exactly once in two
synchronous copies (3.639 s of device-copy duration). Subsequent asynchronous
transfers total 2.015 GB H2D and 0.786 GB D2H, with summed device durations
0.282/0.094 s. The 26 `cudaFree` calls total 0.100 s of API duration; teardown
is not a multi-second explanation in this capture. Per-round stream waits,
launches and event records remain costs, but their overlapping totals cannot
be added to or subtracted from wall time. Excluding the initial index upload
does not remove the remaining host-side computation or repeated dispatch costs.

### What must change to pursue 3x

The sampled main-path worker breakdown places about 50.85% in seeding and 47.62%
in stitching. If that fraction also describes the production critical path,
making all seeding free would cap read-compute acceleration near 2.03x. Applying
the measured 7.29x isolated search improvement to the whole seeding fraction
would yield only about 1.78x under that model. This is an optimistic scope check,
not a measured prediction: prefix handling stays on CPU, the replay uses two
CPU threads, and the sampled intervals are not whole-pipeline wall fractions.

The next experiment should first establish the resident compute timing boundary
above, then capture representative multi-worker stage costs. Move hit expansion,
window construction and the expensive stitching/scoring work into bounded
batches while preserving candidate order, ties, splice rules and output records.
Use CPU implementations of each new batch boundary as differential controls
before CUDA. Include scheduling/packing and difficult-read tails in the measured
cost. Continue only on measured critical-path gains; optimizing the search
kernel alone cannot satisfy a 3x target under the observed stage split.

Keep the current path opt-in. A larger shared queue, additional asynchronous
overlap or a Vulkan implementation is not itself evidence of a speedup.

## Device-resident pipeline migration

The ordered follow-up work packages and the full-computation timing/memory gates
are now specified in [the full-pipeline plan](GPU_FULL_PIPELINE_PLAN.md).
The evidence below remains scoped to the implemented experimental stages.

The owner requested moving as much of the computation as practicable onto the
GPU. Minimize CPU scientific work and repeated transfers, while measuring the
complete-compute 3x target independently. File transport, error handling and
launch orchestration can remain on the host. No stage is considered migrated
merely because its inputs or a helper kernel use CUDA.

| Stage | Current execution | Required next contract |
| --- | --- | --- |
| Input decoding, CR4 clipping, quality statistics | CPU | Bounded raw-read descriptors; preserve clipping, qualities and original reads for BAM |
| Split/prefix/adaptive seed search | GPU in `required` | Exact ordered events, all sparse-distance ties and hard capacity errors |
| Seed consolidation and seed statistics | GPU in `required` | Original duplicate winner, count-before-rejection semantics and exact seed limits; CPU currently copies final pieces |
| SA hit expansion and coordinate projection | GPU in `required`; projected hits return to CPU | Consume hits on device without materializing the full host hit stream |
| SJ splitting and window construction | CPU | Stable seed priority, exact anchor/merge order, chromosome/junction boundaries, window and seed counts |
| Stitching, extension, scoring | CPU | Same include/exclude traversal, splice/indel rules, ties, transcript limits and numerical rounding |
| Alignment selection and statistics | CPU | Same candidate set, mate scores, filtering and output tags |
| Feature/Velocity assignment and barcode/UMI handling | CPU | Same overlap classes, whitelist correction, molecule grouping and graph collapse |
| Cell filtering, EM and count reduction | CPU | Reproducible RNG/numerics and exact existing matrix contract; no blanket exemption as export |
| File serialization/export | CPU | Consume validated scientific records; report separately from computation |

Next work is ordered by the remaining data dependencies:

1. Establish resident alignment-input/result boundaries and a real wall timer
   with workers/device joined. Publish alignment-only and full STARsolo coverage
   separately until all scientific work is included. Stage/worker sums remain
   diagnostics, never the full-compute denominator.
2. Consume the consolidated device seed lists directly for hit/window
   construction. Avoid downloading them only to upload the same candidates
   again. Validate intermediate windows/anchors before replacing CPU consumers.
3. Replace host-owned `Transcript`/`Parameters` access in stitching with explicit
   numeric views and bounded device scratch. `Transcript` currently contains
   vectors, sets and host pointers; copying it to CUDA is invalid. Preserve
   cross-window `maxScoreMate` effects and depth-first candidate order before
   attempting concurrent windows. Do not replace exact traversal with a beam
   search or the previously rejected remaining-seed-length pruning bound.
4. Carry candidate records directly through selection and feature assignment,
   then batch barcode/UMI grouping and reductions. Distinguish integer results
   from rounding/RNG-sensitive EM and cell filtering. Count and report any CPU
   fallback; an unimplemented stage must remain explicit in the coverage table.
5. For every replaced stage, use an independent CPU control, adversarial boundary
   cases and full real-prefix BAM/SJ/count/Velocity comparisons. Bound live arenas;
   overflow must retry safely with smaller GPU work or fail, never drop records.
   Measure repeated same-workload time, RSS, commit, VRAM and transfers before
   proceeding to Vulkan or default promotion.

The device-owned seed scheduler, consolidation and hit expansion are implemented;
window construction and subsequent stages remain a migration plan. Scheduler and
consolidation receipts are under
`data/validation/gpu-resident-pipeline-20261101/`.

### Device scheduler validation (before consolidation)

`candidate.json` freezes the first device-adaptive binary (`99e5d78c...`), before
device consolidation. `mini-cuda/result.json`, `ctest.log` (127/127) and
`memcheck.log` (zero errors) cover the adaptive scheduler; `screen-1m/result.json`
and `contract-5m/result.json` pass the full scientific contract against frozen
main, including BAM/header, SJ, count/axis/filtered/EM and Velocity results.

For 1M reads, host-round CUDA issued 4508 batches versus 250 for the device
scheduler, with the same 4904297 extension calls. For 5M, the device scheduler
issued 1251 batches and no CPU extension queries/rounds. These counts establish
the removed round trips, not a 3x compute speedup. Eight-worker resident
allocations were 27678934148 device bytes and 70254720 pinned bytes.

| 5M path | Whole-job seconds | Process CPU seconds | Peak RSS bytes | Peak commit bytes | Background CPU mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen main | 75.031 | 261.188 | 31451086848 | 31879704576 | 65.61% |
| Device-adaptive seed search | 77.240 | 232.219 | 31710482432 | 60370178048 | 67.92% |

These are single ordered runs on a busy host, not performance acceptance. An
unrelated document-conversion process was observed using roughly eleven CPU
cores during this window; it was not stopped or altered. Process CPU seconds
include all threads, initialization and output, and do not replace compute wall
time. Both the remaining CPU computation and substantial WDDM/driver host
commit remain costs to address. Neither background load nor initialization
times are manually subtracted to invent a speedup.

### Device consolidation validation

`consolidation-candidate.json` freezes binary `90ccd558...` and its source/build
closure. `consolidation-mini/result.json` adds overlapping search starts and
multiplicity filtering to the dense/sparse, strand, clipping, paired and batch
boundary cases. `consolidation-memcheck.log` reports zero errors, including
concurrent slots, duplicate first-winner/count-before-removal semantics, short
split diagnostics, multiplicity-rejected counters and hard seed-limit failures.
`consolidation-ctest.log` passes 127/127; a separate CUDA-disabled build also passes
127/127 (`cpu-control-ctest.log`). `qualification-tests.log` passes all eight
qualification tests, including a GPU-prefix-only execution receipt.

`consolidation-contract-5m/result.json` passes the full scientific contract with
no differing or serialization-only artifacts. A single eight-worker run records:

| 5M path | Whole-job seconds | Process CPU seconds | Peak RSS bytes | Peak commit bytes | Background CPU mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| Frozen main | 73.066 | 252.234 | 31451254784 | 31879708672 | 63.55% |
| Device search + consolidation | 72.328 | 220.125 | 31721013248 | 60387663872 | 61.39% |

Device allocation is 27682604164 bytes and pinned staging is 73924736 bytes.
Host peak RSS rises by about 257 MiB; commit rises by about 26.6 GiB. These are
different measures and neither may be hidden behind the other. Prepared-input
storage now reaches the existing 4 MiB per-worker target around 4064 reads, so
the 5M run uses 1252 batches, with zero CPU seed rounds/extension queries.

This remains correctness/resource screening, not compute-speed acceptance:
there is one ordered pair, the host is busy, and file loading/export are included
in the measured wall time. The result does not demonstrate 3x and does not justify
promoting the experimental backend. The device-prefix receipt check was refined
after this run to accept actual GPU batches with zero extensions; its source is
recorded in the final manifest, while the run retains its original runner hash.

Reproduction:

```sh
python scripts/test_batched_seeding.py --star /absolute/candidate/STAR --reference /absolute/frozen/STAR
python scripts/test_batched_seeding.py --star /absolute/cuda/STAR --reference /absolute/frozen/STAR --cuda
build-cuda/test/star_gpu_seed_slots
python scripts/benchmark_cpu_subset.py --fixture /absolute/fixture --data-dir /absolute/data --output-dir /absolute/new-output --binary original=/absolute/frozen/STAR --binary cuda=/absolute/cuda/STAR --seed-search-backend cuda=required --threads 8 --full-contract
```

The benchmark verifies actual GPU query execution for `required`, not only the
CLI flag. Comparisons include complete BAM records/header, raw and filtered
matrices, EM, axes, SJ, scientific statistics, and all three Velocity layers.
Repeated measurements must retain all rounds and distinguish screening from
calibrated acceptance. No workload-independent speedup or clinical/scientific
equivalence outside the tested profiles is implied.

### Hit expansion validation and resource diagnosis

Evidence is retained under `data/validation/gpu-hit-expansion-20261101/`.
`candidate.json` freezes the initial implementation. Its real 1M-read run
(`screen-1m`) exceeded the hit arena (`mask=16`); this is a retained failure,
not a successful result. Expanding every seed batch in one buffer was unsuitable
for repetitive reads. The repair keeps seeds resident and projects bounded
complete-read chunks, without increasing candidate limits, truncating alignments,
re-seeding reads or silently falling back to CPU.

`chunked-candidate.json` freezes that repair with 24-byte hit records. Both
`chunked-1m/result.json` and `contract-5m/result.json` pass the full scientific
output contract. The 5M comparison includes the repaired CPU path, the previous
GPU search/consolidation binary (`90ccd558...`), and device hit expansion:

| 5M path, 24-byte implementation | Whole-job seconds | Process CPU seconds | Peak RSS bytes | Peak commit bytes | Background CPU mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPU | 68.461 | 225.656 | 31451045888 | 31879483392 | 11.39% |
| Previous GPU search/consolidation | 63.638 | 189.688 | 31719628800 | 60386734080 | 12.65% |
| GPU search/consolidation/hit expansion | 65.327 | 204.391 | 31831359488 | 60568535040 | 13.03% |

These single ordered runs do **not** establish a speed improvement. In particular,
expanding GPU coverage added a bulk download: 1651530618 hit records, or
39636734832 bytes (36.92 GiB), before the CPU could build windows. This diagnosis
supports moving the consumer onto the device, rather than treating another CUDA
kernel as sufficient acceleration. Whole-job time includes loading and export;
the polled mapping interval also includes pipeline I/O and device setup and is
not the owner's pure-computation metric.

The compact follow-up retains 64-bit genome coordinates but uses 32-bit strand
and read coordinates, justified by the bounded read pool. `SeedProjectedHit`
has a checked 16-byte layout. The same hit population needs 26424489888 bytes
(24.61 GiB) of download, one third less; this is a payload-size calculation,
not a measured speedup. Per-worker device/pinned hit buffers fall from 6 to
4 MiB each. `compact-candidate.json` freezes this source and the executable
(`47ba0d57...`); `cpu-ctest-compact.log` and `cuda-ctest-compact.log` pass 133/133
each, `memcheck-compact.log` reports zero CUDA memory errors, and
`mini-cuda-compact/result.json` passes the differential fixtures. Tests include
hit-cap rejection, recovery, bounded chunks and stale generation rejection.

`compact-contract-5m/result.json` passes the full scientific contract with the
same signature for CPU and CUDA
(`26c239b9252287ebf0d592a75cf7dc439e3863eb44a921970a0aa8857aa80bef`),
no differing artifacts and no serialization-only differences:

| Final 5M path | Whole-job seconds | Process CPU seconds | Peak RSS bytes | Peak commit bytes | Background CPU mean |
| --- | ---: | ---: | ---: | ---: | ---: |
| Repaired CPU (`ecffb373...`) | 62.785 | 210.094 | 31451062272 | 31879667712 | 12.86% |
| Compact GPU (`47ba0d57...`) | 65.489 | 199.578 | 31803371520 | 60525371392 | 14.95% |

The final run processes the same 24557584 extension searches and 1651530618
projected hits as the 24-byte version, with 1341 seed batches and 7002 bounded
hit chunks. Resident device allocations are 27732935812 bytes (25.83 GiB), and
pinned staging is 124256384 bytes (118.50 MiB). These are owned allocation totals,
not a measurement of peak device use including the CUDA context/driver. Host
peak RSS is 335.99 MiB above CPU; process commit is 26.68 GiB above CPU. Windows
commit and physical RSS are reported separately.

The observed whole-job ratio is 1.043 (GPU slower in this single ordered pair),
and the background loads differ. This is correctness/resource screening, not a
calibrated regression verdict or evidence of the 3x compute target. The next
implementation boundary is SJ splitting/window construction on resident hits,
then stitching/scoring/selection, so the large hit download can be removed.
FASTQ parsing/decompression and Solo/global reductions follow their own parity
boundaries; a faster seed kernel alone cannot certify the complete-compute goal.

The literature-driven UMI reachability and zero-support EM fixes are shared by
the CPU and CUDA builds. Their counterexamples intentionally change previously
incorrect output; see [the literature review](LITERATURE_ALIGNMENT_REVIEW.md).
GPU migration comparisons use the repaired CPU semantics. This does not assert
that the repaired directional mode is bit-identical to the old incorrect mode,
or that every STARsolo output option is already supported on GPU.

## GPU-resident complete computation

It is technically possible to implement the scientific computations on the GPU
and preserve STARsolo's output contract. This does not mean the current branch
implements all CPU features, or that moving a FASTQ buffer to VRAM is sufficient.
The preferred design keeps the reference resident, streams bounded read batches,
and retains intermediate data on device. Whole FASTQ residency is optional and
must leave space for index, candidates, sort/reduction scratch and count tables.

| Stage | Current ownership | Next acceptance boundary |
| --- | --- | --- |
| FASTQ parsing, quality conversion, clipping | CPU | Device parser/clipping parity, malformed input handling, bounded batches |
| Adaptive seeding and ordered consolidation | GPU in `required` | Exact seed/counter parity; already covered by miniature and real-prefix tests |
| SA hit expansion and coordinate projection | GPU in `required` | Exact downstream output, bounded hit arena, extra transfer/memory cost |
| SJ splitting, windows, stitching, scoring, selection | CPU | Device windows first, then scoring with original candidate order/ties; handle difficult reads without silently discarding work |
| Barcode matching, per-cell/gene UMI grouping and EM | CPU | Use the repaired counting semantics as the oracle; include cross-batch molecules and deterministic representatives |
| Gene/Velocity aggregation, cell calling and global sorting | CPU | Cross-batch/global reductions; all matrix axes, labels, floating updates and Monte Carlo behavior accounted for |
| BAM/matrix serialization, filesystem writes, job control | CPU | Preserve all requested artifacts and error behavior; optimize only after compute bottlenecks |

The last two scientific stages are not independent per-FASTQ-batch operations:
one cell/UMI can span batches, cell calling uses a global ambient distribution,
and coordinate-sorted BAM needs global ordering. A bounded merge/reduction phase
is required even if its arithmetic and sorting ultimately run on device. Exact
floating outputs and RNG behavior require explicit qualification; replacing CPU
reductions with different GPU reduction orders is not automatically equivalent.

GPU gzip decompression is possible via tools such as
[NVIDIA nvCOMP](https://docs.nvidia.com/cuda/nvcomp/), but is not introduced here.
[GPUDirect Storage](https://docs.nvidia.com/gpudirect-storage/overview-guide/index.html)
can avoid a CPU bounce buffer on supported storage/software stacks; it is not an
assumed capability of this Windows/NAS setup. CPU filesystem/control work can
remain small while the principal scientific computations run on GPU. The target
remains 3x complete-compute speedup, including recurring transfers and merges;
neither zero CPU utilization nor percent of ported source lines is the metric.
