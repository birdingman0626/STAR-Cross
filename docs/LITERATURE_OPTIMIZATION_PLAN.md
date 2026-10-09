# Literature-driven exact optimization plan

Date: 2026-10-08. Status: **P0 implemented; P1/P2 screened; conditional production routes not qualified**. See [execution results](LITERATURE_OPTIMIZATION_RESULTS_20261008.md). Qualification is recorded per work package.
Planning baseline: `5a398763d14bc2b3d8b2474e04bf4c6c3688da5d`; freeze the actual execution revision and dirty diff again before any experiment.

This plan refines [the existing optimization plan](OPTIMIZATION_PLAN.md), not a second competing pipeline. Prior accepted/rejected decisions remain in [the results record](OPTIMIZATION_RESULTS_20261006.md). Do not rerun rolling-prefix C2 or rebuild an automatic index-cache service without new hotspot evidence. Do not change the C++17 default, alignment scoring, candidate filters, tie-breaking, supported platforms or default GPU behavior as part of this plan.

## 1. Readiness and source gaps

The three requested algorithm books are locally present and their 1997/2016/2023 copyright identities have been checked. Seven paper full texts are available. That is sufficient to plan and start CPU diagnostics and exact search experiments.

The mm2-gb source gap is now resolved for initial implementation research: the user supplied bioRxiv v1, DOI [10.1101/2024.03.23.586366](https://doi.org/10.1101/2024.03.23.586366), posted 2024-03-27. Methods and evaluation pages were cross-reviewed against the official repository's `gpu_kernel` branch at `8a312e92b5e1cd043950b74306ac2727f7040f46`. This is not verification of the later ACM BCB version, DOI [10.1145/3698587.3701366](https://doi.org/10.1145/3698587.3701366), or local benchmark reproduction. No additional book or paper is required to begin P0. Local source/version receipts and findings are in `knowledge/MM2_GB_REVIEW.md` (not distributed).

Gusfield is an image scan; Navarro has imperfect OCR; Ferragina is searchable. This is a parsing limitation, not a literature shortage. Use targeted rendering/OCR and manually verify the equations that justify a transformation. Do not upload private books to a remote parsing service without authorization. Original books, figures and extracted chapters stay in ignored local storage, outside Git and binary releases.

## 2. Evidence mapped to work packages

| Work package | Source and intended reading | Implementation boundary |
| --- | --- | --- |
| P0: exact contract and cost model | STAR sections 2.1-2.2; Gusfield suffix-array/search material; Ferragina chapters 1, 9 and 10 | Describe what current STAR returns; no replacement of splice-aware scoring |
| P1: cheaper existing search | mm2-fast CPU optimization sections; Ferragina section 9.3 and suffix-array section 10.2 | Locality, exact comparisons and bounded lookup; no ISA requirement for all binaries |
| P2: PWL/PLA replay | Sapling sections 2.2, 2.4, 2.5; PLA-index construction/representation sections | First screen: sampled rank hints with exact original bounds; certified narrowing is a separate, unimplemented step |
| P3: memory-focused index variant | Navarro arrays/bitvectors/text-index chapters; PLA-index | Conditional optional sidecar; no removal of existing SAi until separately qualified |
| P4: conditional stitching/GPU | mm2-plus sections 2.1-2.3; mm2-gb v1 sections 3-4 and pinned source | Requires actual hotspot evidence and STAR-specific equivalence argument |

Book chapter availability is not a claim that all chapters have been reviewed. For each adopted rule, record source section/page, a short original summary, its assumptions, applicable code and the decision. Distinguish theorem, paper benchmark, project hypothesis and engineering threshold. Ferragina's interpolation-search analysis depends on the particular key distribution/data structure; it is not a universal speed guarantee for genomic queries.

## 3. Ordered implementation

### P0: freeze the oracle and remove diagnostic bias

**Targets:** `source/SeedTrace.h`, `test/benchmark_seed_search.cpp`, existing `scripts/run_seed_experiment.py`, `scripts/benchmark_seed_cpu.py` and scientific comparator. Extend these only where necessary; do not create a second capture/benchmark framework.

1. Freeze source/binaries, reference/GTF/whitelist/effective-index hashes, parameters, compiler, ISA and thread count. Preserve existing outputs and use a new result directory per candidate.
2. Confirm capture-off versus capture-on equality with the current miniature fixture. Diagnostic instrumentation remains compile-time opt-in and excluded from performance binaries.
3. Keep the old first-5,000-per-worker capture as an equivalence fixture. Performance screening requires deterministic read-ID sampling across the input and all workers, with observed input-position coverage and no dropped selected requests. Record sampling rules, numerator/denominator, dropped records and mode support. Prefix shortcuts are counted even though they do not become extension replay requests. Do not use thread scheduling as the random seed.
4. Separate counters for prefix shortcuts, extension calls, SA interval widths, compare operations/base inspections, packed-SA loads and multiplicity-range expansion. Use sampled/thread-local counters, not per-query locks or timers in normal builds. Record instrumentation overhead separately.
5. Measure stitching windows, seeds/window and transition evaluations in a separate diagnostic run. Sampling and counters must not change candidate order.
6. Existing capture is one-pass and native-layout dependent. Reject unsupported changing indexes/format or provenance mismatches; regenerate equivalent fixtures per platform rather than pretending old binary captures are portable. Version any format change and test rejection/truncation handling.
7. Build and run the CPU replay with CUDA disabled. CUDA is an explicitly selected optional executor; its allocations and measurements must not enter CPU-only results. Validate captured read identities and retain them for scoped diagnostics.
8. Capture the final ordered seed table per sampled read before stitching, including reads with no seeds. Compare this separately from low-level replay fields. Query equality alone does not qualify the seed-generation call chain.

**Outputs:** frozen run manifest, correctness receipt, workload histograms and current multi-thread hotspot table.
**Exit gate:** instrumentation is scientifically equivalent, bounded and correctly scoped. If query search is not a meaningful current cost, skip P1/P2 production integration and focus on the observed bottleneck.

### P1: optimize the existing exact search first

**Targets:** `source/SuffixArrayFuns.cpp`, `source/ReadAlign_maxMappableLength2strands.cpp`, focused tests under `test/`.

1. Benchmark packed-SA decoding, genome comparison and range expansion separately against the frozen current CPU implementation, not a naïve binary-search replacement.
2. Evaluate one narrow change at a time: safe batched prefetch, cache-conscious access, or exact block/SIMD comparisons. Select the first experiment from P0 evidence; do not implement all candidates automatically.
3. Preserve reverse-complement encoding, N/spacer/junction sentinels, buffer ends, sparse SA distances and the original mismatch/ordering result. SIMD loads must not read past valid buffers; ISA paths must have scalar fallback.
4. Avoid full-read caches, whole-index unpacked copies or a global allocator unless their measured benefit exceeds their explicit memory cost.

**Exit gate:** exact query/seed regression and a repeatable independent gain. A small constant-factor improvement may be retained as a scoped local optimization under the existing policy; it is not automatically a product-level speedup.

### P2: compare Sapling-style PWL and PLA-style bounded predictors

**First implementation surface:** an optional executor in the existing seed replay benchmark. Production `STAR` remains unchanged. No new runtime backend parameter before a successful replay experiment.

1. Pin upstream implementations/algorithms and inspect licenses and dependencies before code reuse. The published PLA tool assumes a single ACGT-only FASTA and a matching suffix array, and requires Linux/C++17/SDSL; STAR's packed, dual-strand, N/spacer and junction-augmented index is not directly accepted. Never remove N or concatenate chromosomes differently to force compatibility.
2. Start with a small uncompressed segment representation implemented from reviewed equations, if licensing permits. Use it to measure usefulness before adding SDSL or compressed encoding. If a dependency is essential, justify it separately; do not bring a suffix-array builder into the read-mapping hot path.
3. Tie the model to the actual effective index content, geometry, alphabet interpretation, k and error bounds. Building on the original FASTA is insufficient when runtime junction insertion changes the searched index. Models become inapplicable on unsupported index mutation.
4. Initial experiments use additive prediction inside the existing SAi search domain. Compare: current optimized CPU, PWL predictor, bounded PLA predictor. Test a small predeclared grid, e.g. k in {14, 18, 21} and rank-error budgets in {16, 64, 256}; reject unsupported encodings before execution. These are search parameters, not validated defaults.
5. A predicted hit is not a certificate of maximal-match length or full multiplicity. Establish exact bracketing, all-match boundary expansion and a global-optimality condition over the original eligible interval. If any certificate fails, call the original search over its original bounds. Always fall back for unsupported query lengths/alphabet/index modes in the first prototype.
6. Record prediction time, exact-search time, fallback rate, full interval width, build/load cost and resident/peak bytes, stratified by repeats, strand, query length and sparse geometry. Track worst cases, not only median easy queries.
7. Reject a model whose fallback/search overhead outweighs the current prefix-index path. Report build cost separately and compute the measured break-even query count; do not hide it in warm throughput.

The implemented first screen deliberately uses an **uncertified rank hint**, not an error-certified index: at most one predicted interior probe changes the search schedule; the original interval and exact boundary expansion remain intact. PLA's error parameter constrains sampled points only. PWL uses fixed bins and does not use that error parameter. Neither implementation advertises global rank-error guarantees or replaces the complete published index. Only reconsider certified narrowing if this low-cost feasibility screen provides evidence that its additional construction/search complexity is justified.

**Search gate:** all four search outputs (length/lower/upper/multiplicity) match exactly and the candidate beats optimized CPU on representative held-out queries. This permits experimental call-chain integration with exact CPU fallback.

**Integration gate:** compare the ordered seed tables per read, then complete BAM/header/SJ/Solo outputs. Only a qualified integration can become a usable opt-in production path. No new backend is promoted solely from synthetic or reused training queries.

### P3: optional memory reduction, not mandatory model stacking

Proceed only if P2 shows a useful time/memory trade-off or current SAi memory is a confirmed constraint.

1. Evaluate replacing part of the direct-access prefix table with an optional compact predictor, rather than permanently keeping both full structures.
2. Keep the existing index format and loader as default. An optional sidecar must have content/version/parameter signatures, explicit loading checks and atomic completion. Never overwrite a user's existing index. Truncated, foreign-endian, stale or incompatible files fail validation before use.
3. Verify peak model-construction memory, not only final model size; enumerate keys in a streaming/bounded way where possible. Avoid duplicating the complete suffix array in RAM.
4. Quantify construction time, peak memory, persistent bytes and complete-task wall time. Reuse is only valid for the same effective-index contract; two-pass updates remain outside support until tested.

**Exit gate:** demonstrable memory reduction with acceptable measured runtime, or product-level speedup within the memory budget. If this route cannot preserve compatibility cheaply, retain the replay evidence and defer it.

### P4: conditional alternatives

- **Stitching:** only after P0 shows a significant cost. Start with proven cheap rejection of impossible transitions. The current implementation already rejects excess exons and fully overlapping same-fragment transitions before copying; do not claim these existing guards as a new optimization. The first follow-up candidate instead removes redundant immutable-parent copies in synchronous recursion, retaining mutable branch/terminal copies and exact traversal. Document a sound bound before pruning; a previous incomplete upper bound must not be reintroduced. RMQ/Fenwick/interval trees are not automatic O(k log k) replacements for splice-dependent transition scoring.
- **GPU:** only after CPU optimization and representative transfer-inclusive replay indicate opportunity. The available mm2-gb v1 and pinned source support initial scheduling research: separate work by measured cost, aggregate within bounded buffers and prioritize expensive independent tasks. Prove independence under STAR scoring before splitting any seed chain; a minimap2 zero-successor range does not certify a STAR boundary. Retain CPU for small workloads and calibrate the dispatch threshold locally, not from mm2-gb's 512-anchor default. Do not copy its single-host-thread/single-stream restriction, large host-buffer policy or forced `max-chain-skip` setting into STAR. Reuse current CUDA replay and the CPU-first scheduler gates in the existing plan; include initialization, upload, packing, flush and tail batches in whole-task measurements. Its Figure 8 score-generation throughput is not an end-to-end STAR speedup. Keep source-code MIT permission separate from the PDF's CC-BY-NC license, and audit all reused files/dependencies before incorporation.
- **BiWFA/full LCP-RMQ:** deferred unless an appropriate hotspot and exact scoring/domain match are established. Neither is an obligatory new dependency.

## 4. Correctness matrix

| Level | Cases | Required evidence |
| --- | --- | --- |
| Unit/property | Exhaustive tiny ACGT queries, absent/short queries, repeats, N/spacers, both strands, packed boundaries, overflow, zero/tail batches | Original CPU is oracle; invalid requests rejected; identical search fields and seed order |
| Miniature integration | Dense and sparse D=3 plus another supported sparse geometry; annotated/novel junctions; paired overlaps and multi-mappers | Complete BAM/header/SJ/Solo comparison, not aggregate mapping rate |
| Supported modes | One-pass default; two-pass/chimeric/WASP/other remapping modes individually | Either full equivalence or explicit CPU fallback before candidate commitment; unsupported modes not advertised as accelerated |
| Real data | Existing 100K/1M fixtures; held-out deterministic R1/R2-synchronized segments from different library positions; then 5M/10M and a full representative library | Source read-only; independent candidate correctness and scale/memory evidence; prefix does not qualify full-library cell calling |
| Platforms | Native Windows x86-64 first; Linux x86-64; Linux ARM64; macOS x86-64/ARM64; existing s390x support | Existing CI plus candidate-focused tests; signedness/alignment/endian/ISA coverage; CUDA optional and CPU-only build unaffected |

Correctness includes every alignment record and relevant tag, splice rows, raw/filtered/EM matrices and axes, cell calls, and spliced/unspliced/ambiguous layers. Dates and execution timings may be normalized; unexplained scientific differences may not. Equal scores with different traceback/CIGAR or tie winners are failures for this exact-compatibility route.

## 5. Performance and memory gates

These are **engineering selection criteria**, not literature-derived laws or predicted gains.

- At least three order-alternated baseline/candidate pairs, profiling runs separate; increase repetitions when noise overlaps the apparent effect. Keep CPU thread counts, reference/input/options and output profiles identical.
- Current product gate remains median end-to-end reduction >=10%, each pair improves, and no unexplained >5% median regression in other qualified profiles. Retain raw per-pair numbers and machine-load observations.
- Initial query replay aims for >=15% median reduction including predictor, packing and fallback; this is a screening threshold only. Measure any build/load cost and cold/warm behavior separately.
- Provisional additive CPU budget: model <=64 MiB; all new persistent/transient buffers combined <=128 MiB, and measured total peak growth <=1% where reliably measurable. Construction memory is included. Prefer P3 memory savings; no silent increase beyond budget. If measurement uncertainty prevents a memory claim, report UNVERIFIED and repeat rather than calling it passed.
- Measure whole-process wall time to final output closure, peak host memory and, for GPU routes, device/pinned memory. Hashing external receipts is outside algorithm timing, but normal in-program model validation/load is inside it.
- Stop on scientific mismatch, an unproven search/pruning certificate, unacceptable memory, inconsistent gains, or complexity without useful benefit. Rejected code leaves the default path; keep its receipts and reason to prevent repeated investigation.

## 6. Deliverables and implementation boundary

Implement one work package per reviewable change: P0 diagnostics, P1 chosen exact optimization, P2 replay prototype, P2 optional production integration only if qualified, and P3/P4 only if triggered. No promise to implement every technique.

Each result records: hypothesis; source claim/page and applicability; frozen inputs/code/parameters; correctness results; paired time and memory; accepted/rejected reason; remaining support gaps. Source literature is private local research material; public documentation contains original summaries and references only.

Execution begins with **P0**, followed by the P1/P2 experiment justified by measured costs. P3/P4 require their stated triggers. Record deferred and rejected branches as decisions, without presenting them as implemented or qualified.
