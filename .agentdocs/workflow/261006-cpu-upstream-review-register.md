# CPU / upstream review register

Reviewed: 2026-10-06. Scope: STAR-Cross CPU prerequisite plan, not a GPU implementation.
Baseline source: `1f9aba49acad69ead85cc938ccbe2c4785ed62d7` plus the pre-existing
7-line `ParametersSolo.cpp` Velocyto/Gene change (preserved, not authored by this review).
Frozen WSL baseline binary SHA256:
`7322d7066a362418dcdb36d7244f249a62c2b1ef078ce1cee4f9bc265b600a6b`.

## Decisions (do not rediscover without a review trigger)

| Item | Evidence / applicability | Our decision | Revisit trigger |
|---|---|---|---|
| [PR 2692](https://github.com/alexdobin/STAR/pull/2692) | Order-only dependency already in `source/Makefile`; CMake generated-header dependency also exists | No duplicate patch; exercise clean parallel CMake build | Build-system change or reproduced Makefile race |
| [PR 2680](https://github.com/alexdobin/STAR/pull/2680/files), head `c57ffff8eb3afdd3c74d8b98c64c31e291b07e4c` | Removes RAM limit checks and computes a chunk estimate, not complete peak memory; local prefix staging uses the RAM budget | Reject transplant; retain explicit RAM limit. A new estimator requires separate peak-memory measurements | Reproducible incorrect estimate with memory accounting |
| [PR 2011](https://github.com/alexdobin/STAR/pull/2011/files), head `77232a31936f0809d7ad02454c7c2b1739cd84f2` | Its B branch calls the Z-string writer, losing typed-array semantics | Reject patch; use bundled HTSlib SAM parser, bound the resulting auxiliary bytes before copy; retain tag filtering | Parser change, unsupported valid tags, or hot-path profiling evidence |
| [Issue 2223](https://github.com/alexdobin/STAR/issues/2223) | FASTQ comments discarded at chunk ingestion, before `readNameExtra` is populated | Preserve comments separately for each mate after internal bookkeeping; keep existing unmapped-output prefix and mapping suffix | Header-dependent consumers or overly long headers |
| [PR 2071](https://github.com/alexdobin/STAR/pull/2071/files), head `b386d9d12b41383bece04cb1ab70665a62b00d9f` | 26-file patch mixes allocator/build changes with biotype output; biotype already loaded in Transcriptome | Only optional `soloOutFormatFeaturesGeneField3 +`; unchanged 10x default, explicit non-modality warning | Feature-reader compatibility or changed annotation schema |
| [Issue 2600](https://github.com/alexdobin/STAR/issues/2600) / [1381](https://github.com/alexdobin/STAR/issues/1381) | Version, chemistry, strand and filtering choices affect comparisons; no universal GeneFull fix follows | Diagnostic checklist, not automatic count-rule changes | Matched-design discrepancy with attributable stage |
| libdeflate | Bundled HTSlib already has the implementation behind HAVE_LIBDEFLATE | Optional CMake flag, default OFF; require installed library/header, report actual link; zlib retained | Representative BAM-writing benchmark demonstrates end-to-end gain |
| mimalloc | No measured allocator bottleneck in this review | Defer dependency and interception code | Measured allocation/lock hotspot and platform ownership tests |
| suffix cache / pruning | Remaining seed lengths omit terminal extension and final score terms in `stitchWindowAligns`; not a proven admissible bound | Remove the unproven non-legacy pruning branch; do not cache it. This is a correctness precaution, **not** proof that a particular dataset was misaligned | A complete bound proof plus adversarial no-prune oracle and profiling |
| EmptyDrops binary search | Existing `lower_bound` implementation and unit coverage | No duplicate implementation | Measured new hotspot or failing strict-comparison oracle |
| validation scripts | Missing tests, reads and references could yield success; CTest regex did not match discovered names | Require unit tests and inputs/reference; unique retained output; honor reference executable | New output profile (historical 21-file profile is not BAM/Velocity qualification) |

These upstream PRs were open/unmerged when inspected; exact inspected heads above
are the reference, not a claim about future upstream state. No patch is accepted
solely because an upstream author requested it.

## Counting-discrepancy diagnostic contract

Before changing count logic, record chemistry (3'/5'), barcode/UMI layout and strand,
STAR and Cell Ranger versions (issue 2600 used CR 9.0.0), FASTQ identity, FASTA/GTF
hashes and filtering, adapter/read trimming, Gene vs GeneFull rule, multimapper
handling, whitelist correction, UMI collapsing and cell-calling parameters. Compare
raw counts before filtered cell sets, and attribute differences to stages. The strand
suggestion in issue 1381 is contextual, not a default for all datasets. Do not turn
a requested matching output into the objective at the expense of stated semantics.

## Reproduction and verification

WSL Ubuntu, GCC 13.3, bundled HTSlib 1.21, system zlib 1.3; optional installed
libdeflate 1.19. Parasail 2.6.2, doctest 2.4.11. Build with:

```sh
cmake -S source -B <build> -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON -DSTAR_USE_LIBDEFLATE=OFF
cmake --build <build> --target STAR star_tests --parallel 8
ctest --test-dir <build>/test --no-tests=error --output-on-failure
python3 scripts/test_cpu_upstream.py --star-exe <build>/STAR --ref-exe <frozen-before>
```

Repeat build/tests with `STAR_USE_LIBDEFLATE=ON`; retain the OFF binary before
reconfiguring. Library version is environment-locked in this execution record,
not silently downloaded or pinned by CMake. `USE_SYSTEM_HTSLIB=ON` manages its
own backend; combining it with STAR_USE_LIBDEFLATE is an explicit configuration error.

The tiny integration fixture covers real genome generation, mapped/spliced/unmapped
BAM, coordinate-sorted and transcriptome BAM, baseline record/junction comparison,
mate-specific comments, Normal/BySJout, SAM-input B arrays and whitelist, feature
output modes, and Gene/GeneFull/Velocity raw matrices and axes. It does not substitute
for large-dataset performance, BAM deduplication/other specialized writers, full
two-stage junction-retention coverage, native Windows/macOS or actual big-endian
runtime qualification.

Initial baseline: 75/75 unit tests passed. Inspected final execution results are
recorded below. At that review, `data/fastq` was empty and the historical wrapper
could not run. A later user-supplied real paired prefix is isolated outside that
canonical directory; see the [real-input record](261006-real-cyno-prefix-regression.md).

### Execution evidence

- zlib and libdeflate configurations: full STAR and test targets built, 78/78
  unit cases passed each. BGZF tests cover 200 KB compressible/incompressible
  payloads, multithreaded blocks, EOF, index dump/load and uncompressed random seek.
- libdeflate was not merely linked accidentally: HTSlib compile flags include
  `HAVE_LIBDEFLATE=1`, archive symbols reference libdeflate compression/decompression,
  and final executable resolves `libdeflate.so.0`; installed version is 1.19.
- Configuration negative checks reject both conflicting system/bundled options
  and nonexistent cached library/header paths. Final build cache restored to OFF.
- 2 harness regression methods passed with empty references, missing individual
  reference/candidate files, changed bytes, absent/zero-discovered tests, absent
  reads/references, reference-executable use and zero processed-read rejection.
- The actual repository-input validation reports failure for the empty FASTQ
  directory after executing 78 unit cases; no full-data PASS is claimed.
- Real-STAR fixture comparison: frozen original to final libdeflate candidate
  passed (`/tmp/star-cpu-regression-2z5131m9/result.json`); final zlib vs final
  libdeflate comparison passed and is retained at `/tmp/star-cpu-regression-ilqt0svw/result.json`,
  including oversized-header rejection.
  These compare BAM records, not compression bytes; exact integer matrices and
  axis files on this tiny fixture are invariant.
- Final libdeflate binary SHA256:
  `e33dfe1a7f673f7a0bfc12f13e0678dc2219b6960503accc8364fb96c3344066`.
  Final zlib binary SHA256:
  `fbea81f9197f44b648fdccfb614832cb40fcf44c9f45597d411498148bb0bb16`.
- Current integration script SHA256:
  `d31653557638be7d1379c90e757d415e9ca4fc5372e1ae3ebea5d85915e4c433`.
  Harness regression script SHA256:
  `c6b2efc4e96ef2ee6f1e50497cbabd2357b19b7a3e4baa7e0454fe067b3ee504`.
- Build/configuration/unit evidence retained under
  `/home/ubuntu/star-cross-validation`. Evidence was collected before commit; existing
  `ParametersSolo.cpp` still has its original 7 added lines only.

No end-to-end speedup, large-reference RAM qualification, GPU acceleration or
cross-platform qualification follows from these small tests. Next useful step:
use the source-identified real FASTQ fixture, profile default and
legacy scoring separately, quantify the removed pruning's runtime cost and
libdeflate's BAM-writing benefit before promoting a performance default.

## Related prior reviews

- [CPU stability / GPU entry](261006-cpu-stability-gpu-entry.md): follow-up independent
  upstream oracle found and repaired the pre-existing Parasail striped clipping
  discrepancy. Corrected 1M-pair run/repeat match upstream integer counts, junctions
  and complete BAM records. Profiling prioritizes a bounded resident seed-search
  experiment, not unmeasured GPU UMI work; full-library/platform scope remains open.

- [GPU migration roadmap](261006-gpu-acceleration-research-and-roadmap.md): CPU
  semantics first, independently profiled offload, no proprietary code migration.
- [GPU benchmark contract](261006-gpu-validation-and-benchmark-plan.md): frozen
  oracles and artifact-specific qualification. Parabricks OCI download is complete,
  but no container benchmark or GPU implementation is claimed.
- [CPU plan](261006-performance-optimization-and-upstream-sync-plan.md): this register
  supersedes its earlier unverified-upstream table; accepted/deferred decisions above
  are authoritative for this review.
