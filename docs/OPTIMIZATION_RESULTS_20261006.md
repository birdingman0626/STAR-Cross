# Native Windows seed optimization verification

This is an engineering benchmark on a frozen 1,000,000-pair Cyno TMJ FASTQ prefix, not a full-library qualification. Eight CPU threads; identical clipping, GTF, whitelist, Gene/GeneFull_Ex50pAS/EM/EmptyDrops/Velocyto and unsorted BAM options. No production data or earlier results were overwritten.

## C1: avoid unnecessary temporary seed arrays

For the ordinary dense suffix array, commit the sole result directly instead of allocating three temporary vectors for every seed. Sparse indexes retain the original competition, with storage limited to valid distances. No heap-backed cache or per-read state was introduced by C1.

Three sequential pairs alternate execution order (baseline/candidate, candidate/baseline, baseline/candidate). Full-process wall time includes loading, annotation, mapping, Solo and writing. No global cache flush; these are cached/order-alternated measurements, not cold-cache claims. Frozen binaries and input content fingerprints are recorded.

| Pair | Baseline seconds | C1 seconds | Reduction |
| --- | ---: | ---: | ---: |
| 1 | 60.237 | 58.621 | 2.68% |
| 2 | 63.586 | 56.615 | 10.97% |
| 3 | 61.084 | 55.405 | 9.30% |
| Median | 61.084 | 56.615 | 7.32% |

Median peak working set is approximately 29.32 GiB in both profiles. Maximum observed candidate peak is 65,536 bytes above maximum baseline peak (0.000208%); this is not a demonstrated increase or decrease in memory. Peak process commit is also essentially unchanged. These Windows process counters do not measure OS file cache or system-wide memory.

Every completed run matches baseline-1: all 1,254,350 BAM alignment records including multiplicity and tags, junction rows, every declared Solo matrix/axis/feature statistic, filtered cells and all three Velocity layers. EM output is byte-identical; no floating-point tolerance was needed. Non-scientific dates/runtime fields are excluded from Log.final.out. The first comparator incorrectly included start/finish dates; verification was repaired and rerun on the preserved outputs without changing producer data or timing.

Local evidence: `data/validation/cpu-seed-optimization-20261006/scratch/result.json`, frozen executables and per-run commands. Supplementary verification also passed BAM reference dictionaries and SQ/RG/HD semantics and **every** file under Solo.out, including Barcodes.stats and UMIperCellSorted.txt.

## C2: rolling prefix experiment

Candidate used one previous window per direction, invalidated for every mapOneRead generation; no whole-read prefix table. Unit checks exercised both directions, widths 1–31, repeated/adjacent/discontinuous queries, variable widths and invalidation. Dense and sparse-D=3 miniature end-to-end regressions passed against the frozen baseline. Three full scientific comparisons also passed. Versus C1, paired seconds were 53.320/51.495, 54.842/51.359 and 52.804/55.105. Median reduction was 3.42%, but pair 3 regressed 4.36%. **Rejected:** no consistent independent benefit; rolling state/helper/test removed from production source. Frozen candidate and execution receipts are retained under `prefix/`; do not use STAR.prefix.exe as the supported optimized binary.

## B1 / G1: bounded real-query replay

An explicitly enabled diagnostic build captures production extension requests after clipping and prefix lookup. Ordinary builds compile this instrumentation out. A 10,000-pair one-pass run recorded the first 5,000 extension requests of worker 0, and counted 45,321 dropped extension requests. Prefix-only shortcuts are not recorded; this is not a complete query/chunk distribution or deterministic read sample.

Replay uses the actual augmented Genome/SA and explicit record count, checks format/bounds before CPU replay, then compares all four CPU/CUDA result fields. All 5,000 recorded requests match both production CPU results and CUDA. The query sample costs only about a millisecond; its three paired CPU/GPU times (ms) are 1.255/1.227, 1.355/0.897 and 0.703/0.846. GPU index setup is 3.925 seconds and device buffers occupy 27,200,827,057 bytes. These short-query timings do not establish an end-to-end win; no default GPU search or asynchronous scheduler is promoted.

The first diagnostic metadata writer streamed unsigned-char GstrandBit as a character. The failed replay is preserved; the metadata repair is documented next to the capture, cross-checked against source-index metadata and captured packed-SA geometry. No sequences or CPU result records changed. The corrected diagnostic build passed a fresh miniature production capture: 3,198 requests replayed in batches 1/7/256, and capture-on/off BAM records, scientific headers and junctions are identical. Unknown format, truncated capture and invalid bounds are rejected before CPU/GPU search. Miniature evidence: `star-seed-fixture-_sa9u926` in the retained OS temporary directory. One-pass capture only; unsupported changing-index modes are rejected.

Local evidence: `data/validation/cpu-seed-optimization-20261006/capture/`, `replay/` (failed diagnostic format) and `replay-r2/result.json` (successful scoped replay).

## Effective-index reuse and remaining gates

The original index's annotation files differ from the effective runtime annotations (e.g. exonInfo.tab and transcriptInfo.tab). Removing --sjdbGTFfile against that original index is **not** an equivalent optimization. Reuse must persist the actual effective index via existing --sjdbInsertSave All, bind its source inputs/options/version and verify outputs before adopting it. No new index-cache framework is needed.

The persisted effective index and source-input signatures were saved under `effective-index-build/`. A fresh 10,000-read reuse pilot matched all BAM records/headers, SJ, every Solo output and scientific final-log field. Final-build sparse-D=3 miniature regression and 83 STAR CTest cases passed (three unbuilt third-party Parasail executables excluded, not counted as passed).

During the subsequent full paired reuse benchmark, unrelated large processes and Windows Memory Compression were present. Baseline-1 index loading took about 77 seconds, substantially above earlier measurements. `reuse/ENVIRONMENT.md` records observed pressure; this is not a quiet-machine benchmark or a claim that all elapsed-time differences are algorithmic. No unrelated process was stopped.

Final CPU build (C1 only) plus opt-in effective-index reuse, compared with the original pre-C1 uncached profile:

| Pair | Original seconds | C1 + saved index seconds | Observed reduction |
| --- | ---: | ---: | ---: |
| 1 | 112.928 | 53.579 | 52.55% |
| 2 | 72.892 | 50.195 | 31.14% |
| 3 | 68.787 | 53.362 | 22.42% |
| Median | 72.892 | 53.362 | 26.79% |

All six complete-output comparisons and final content-fingerprint validation passed (`PASS_SCIENTIFIC_COMPARISON`). Every candidate contains exactly the same 1,254,350 BAM records as baseline-1; dictionaries/SQ/RG/HD, SJ, every Solo file and scientific final-log field also match exactly. Median peak working set falls from approximately 29.32 to 27.61 GiB (about 1.71 GiB / 5.83% lower); peak process commit also falls. The three candidate peaks differ by less than 100 KiB. All paired timings improve, but background/cache effects limit attribution; no general 26.79% speed guarantee is claimed. Local receipt: `data/validation/cpu-seed-optimization-20261006/reuse/result.json`.

CPU wavefront scheduling, default GPU integration, UMI hashing, LCP/RMQ and stitching changes remain conditional on hotspot/cost evidence. This iteration must not be called complete implementation of all optional work packages or qualification of whole source libraries.

## Reproduction and use boundaries

The normal CPU build retains C1 only. Build with `STAR_ENABLE_CUDA=OFF` and `STAR_CAPTURE_SEEDS=OFF`; no new runtime switch is required for C1. `build-native-cpu-20261006/STAR.exe` and `frozen/STAR.final.exe` are the final local builds; baseline and rejected rolling-prefix executables remain separate evidence.

The reusable index is opt-in, not an automatic cache:

1. With the same qualified source index, GTF and SJ/GTF options, run existing `--sjdbInsertSave All` into a **new** output prefix. Its `_STARgenome` directory contains the actual effective index and annotations; do not overwrite the original index.
2. Record source-content hashes, engine version/binary, SJ/GTF feature/tag/prefix/overhang settings and all saved-index hashes. A filename match is insufficient. Validate the intended analysis outputs against the uncached profile before reuse.
3. For qualified repeated runs, use `--genomeDir <saved _STARgenome> --sjdbGTFfile -`. The saved annotations are used; the original GTF is **not** reloaded. Changes to reference, GTF, options or required engine semantics require a new build/requalification. Sample-specific two-pass junctions are outside this one-pass qualification.
4. Persisting this fixture's effective index costs about 27.7 GB additional disk space and one-time preparation/write/validation work. This cost is not charged to every subsequent run, but must be considered for a one-off task. No general automatic invalidation/cache service has been implemented. The benchmark's external content audits are outside timed STAR executions; the regular STAR index loading/metadata checks remain inside them.

`scripts/benchmark_seed_cpu.py` accepts frozen `--baseline`, `--candidate`, a fingerprinted `--reference-run`, and a **new** `--output-dir`. It runs three alternating pairs by default, measures process wall/peak-memory, then checks complete scientific outputs. `--candidate-reference-run` supplies a separately fingerprinted command profile for index-reuse comparisons. `--verify-only` rechecks preserved runs without replacing timings. Inputs are hashed before and after execution; nonmatching/missing evidence fails validation.

Example (paths are local evidence, not distributable datasets):

```powershell
python scripts/benchmark_seed_cpu.py --baseline data/validation/cpu-seed-optimization-20261006/frozen/STAR.baseline.exe --candidate data/validation/cpu-seed-optimization-20261006/frozen/STAR.final.exe --reference-run data/validation/native-gpu-20261006/pipeline-r1/cpu --candidate-reference-run data/validation/cpu-seed-optimization-20261006/effective-index-build/reuse-profile --output-dir data/validation/cpu-seed-optimization-20261006/reuse-new
```

Permanent checks: `python scripts/test_scientific_comparison.py`, `python scripts/test_run_seed_experiment.py`, and `scripts/test_cpu_upstream.py --sa-sparse 3` with explicit frozen binary paths. The optional diagnostic `--capture-star` path in `scripts/test_gpu_seed_integration.py` validates producer capture, on/off equality and replay with tiny/tail batches; ordinary users do not need it.
