# Release Validation Test Plan

Status: reference procedure, revised 2026-10-06.
The [GPU validation contract](261006-gpu-validation-and-benchmark-plan.md) defines
additional acceleration/Velocity coverage. This revision corrects the older
universal 21-file/byte-equality gate. The [CPU review register](261006-cpu-upstream-review-register.md)
records the subsequently implemented fail-closed harness changes and their verification.

## 1. Freeze the reference

Record known-good source commit/diff, binary hash, compiler/libraries, effective
arguments, data/index/GTF/whitelist hashes and default/legacy mode. References are
immutable and must be complete for the declared profile. A missing reference is
UNVERIFIED, not SKIP-to-PASS. Never replace the baseline because a candidate differs.

Use unique run directories. The prior examples deleted smoke/reference/output
directories; preserve old evidence and create a new run instead. Paired subsets
must preserve read IDs. Verify cDNA vs barcode roles from chemistry/read structure.
Historical scripts pass R2 before R1; the old table's opposite role labels were not
reliable and must not be copied into new commands.

## 2. What the existing harness proves

| Entry point | Actual scope and limitations |
|---|---|
| scripts/smoke_test.sh BINARY DATA_DIR | Runs a matrix-only droplet subset and checks nonzero input reads; does not establish matrix equality and removes its temporary output |
| scripts/validate_build.sh | Requires actual unit-test discovery and smoke inputs/reference; supplied reference executable is run; unique outputs retained; only six raw Gene/GeneFull artifacts compared |
| scripts/release_compare.sh REFERENCE CANDIDATE | Requires all 21 historical Solo files on both sides, after CR removal; lacks BAM/SJ/Velocity and numerical-float contracts |
| scripts/test_cpu_upstream.py --star-exe BINARY --ref-exe FROZEN_BINARY | Tiny real-STAR fixture for alignment records, junctions, main BAM writers, SAM tags, mate comments, Gene/GeneFull/Velocity matrices and feature modes; not a large-workload or biological qualification |
| scripts/test_validation_harness.py | Corruption/missing-test/input/reference fixtures for fail-closed harness behavior |

A successful exit from these scripts is supporting evidence only. Extend/wrap their
coverage checks before using them as general automatic release gates. The narrow
missing-input/reference/test gates are implemented, not the full GPU contract.

The smoke script expects fastq/R1_1M.fastq, fastq/R2_1M.fastq,
genome_cynomolgus and whitelists/3M-february-2018.txt below DATA_DIR. It requests
Gene and GeneFull_Ex50pAS with no BAM. Run the scripts using their documented
arguments, but retain independently generated output for semantic comparison.

## 3. Validation levels

| Level | Inputs | Required interpretation |
|---|---|---|
| Build/unit | Actual CTest-discovered cases | Record executed count and failures; zero tests is not a pass |
| Smoke | Paired approximately 1M subset | Startup/processing and raw-output comparisons; not cell-filter qualification |
| Full historical regression | Existing approximately 434M dataset | Counts, filtered identities and statistics for its exact configured profile |
| Extended release regression | Representative BAM, junction, optional mode and Velocity fixtures | Qualify only the modes actually covered |

Historical runtime estimates were machine-specific. Measure new runtime and resource
use rather than promising five-minute/two-hour completion.

## 4. Artifact and comparison contract

Declare an expected artifact list per profile before running, including producer
completion status. Adding features legitimately changes the list; a fixed 21-file
count cannot qualify every run. Missing required files fail even if both reference
and candidate lack them.

- Raw/filtered integer matrices: exact values, coordinate support, dimensions and
  feature/barcode identities; check documented axis order separately.
- Filtered cell calls: exact barcode identities for the same reproducible mode.
- EM/floating values: same-environment deterministic paths should reproduce; any
  cross-platform tolerance must be justified and fixed before comparison, include
  near-zero/NaN handling and preserve downstream decisions. A text diff does not
  implement numerical tolerance.
- Scientific statistics: compare parsed stable integer/count fields exactly. Parse
  Log.final.out scientific fields rather than discarding the whole file for its timing.
- SJ.out.tab: compare full sorted junction rows/counts where required; include a
  dedicated junction-producing fixture. Do not blanket-exclude it because BAM is off.
- BAM: compare complete normalized record semantics, multiplicity, primary flags,
  tags, mates and declared sort order; validate index retrieval. Compression-module
  tests compare decompressed binary BAM bytes, not compressed bytes or SAM text.
- Velocity: explicitly exercise Gene plus Velocyto, require spliced/unspliced/ambiguous
  matrices and their axes. Add GeneFull_Ex50pAS where supported. Compare against the
  matching CPU implementation; BAM-derived velocity is a separate method comparison.

Line endings and runtime metadata may be normalized only under a recorded rule;
normalization must not erase scientific differences. See the GPU contract for
detailed comparator and fault-injection cases.

## 5. Release verdict

Report PASS, FAIL, SKIPPED, UNVERIFIED and NOT_APPLICABLE per required test/artifact.
Missing data, zero discovered tests, unexecuted mandatory modes, missing reference
files or stale provenance block qualification. A legitimate empty result still
requires valid schema/axes and a completed producer record.

A release claim states the binary/source hashes, tested platforms/modes, coverage,
known limitations and reproduction commands. The historical 21-file check alone
cannot approve GPU support, Velocity, BAM correctness or all-platform equivalence.
Keep comparator summaries and raw evidence with the run; do not publish a new
release solely because the plan or convenience script reports completion.
