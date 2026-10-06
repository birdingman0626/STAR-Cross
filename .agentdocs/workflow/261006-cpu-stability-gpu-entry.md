# CPU stability and experimental GPU entry

Date: 2026-10-06. Status: CPU_ORACLE_QUALIFIED_FOR_SCOPED_GPU_EXPERIMENT.
Scope: WSL Ubuntu / GCC 13.3, default non-chimeric CB16/UMI12 cynomolgus
engineering prefix. This is not full-library/cross-platform/GPU certification.

## Independent correctness, not old-output preservation

Official STAR 2.7.11b was downloaded from the upstream release commit `b1edc12`:
`https://raw.githubusercontent.com/alexdobin/STAR/b1edc12/bin/Linux_x86_64/STAR`.
It has SHA256 `d66601ed3589534cba53f897350e0b473cd99f4411be30a2cc7f4796d9a36920`,
identical to this repository's bundled upstream executable. No NVIDIA binary was
inspected, reversed or used as an oracle.

The old pruning ablation localized a real change but did not prove correctness.
Independent upstream agreement is the additional gate. Initial upstream comparison
exposed a second, pre-existing issue: the Parasail striped adapter recurrence did
not preserve Opal overlap scores/endpoints at the current low gap penalties.

An isolated diagnostic compiled upstream Opal's unchanged arithmetic (native AVX2
header in place of SIMDe for this x86-only diagnostic) alongside this ClipCR4 and
Parasail scan. On all 1,000,000 real cDNA reads:

- Striped vs Opal: 6,323 different scores/endpoints; 48 different applied clips.
- Scan vs Opal: **zero** different scores/endpoints.

Diagnostic source/binary: `/home/ubuntu/star-cross-validation/clip-oracle.cpp`,
`clip-oracle`, `opal-upstream.cpp`, `opal.h`. These are validation-only external
sources, not newly vendored production dependencies. Production change uses the
existing Parasail scan API. Removed an unused per-read debug string; replaced the
128-base silent custom-adapter truncation with a reused growable buffer.
Unit tests use an independent scalar affine overlap recurrence and endpoint policy,
including unknown/short/adapter-rich reads and adapters longer than 128 bases.

## Executed evidence

- Updated zlib CPU binary:
  `e52cb717fa8ca9b0413dfc372ddb7e91974de9373d512e9efab12bdaf7dfbb86`.
- Updated instrumented binary (`-pg`, separate build):
  `823c92b19d641fc7ec1b77e81453fb7c331bf6312e6cff006b4f8d2a09731e3f`.
- **80/80** CTest cases passed; **7/7** Python test methods passed, including
  multiple negative gate scenarios, integer-matrix validation and pair extraction.
- Miniature STAR regression after clipping fix: `/tmp/star-cpu-regression-0k0hb30j`.
  Sorted/transcriptome BAM, comments/SAM tags/biotype and three-layer counts passed.
  Final single-thread/no-reference integration is retained separately when complete.
- Real input remains the existing synchronized 1M-pair engineering prefix. Full
  input/index/GTF/whitelist contents are hashed; source library was not modified.
- Independent upstream run:
  `/home/ubuntu/star-cross-validation/real-tmj-official-oracle-20261006/official`.
- Corrected candidate and repeat:
  `/home/ubuntu/star-cross-validation/real-tmj-clipfix-qualification-20261006`.
- First corrected candidate: all five integer matrices/axes exactly agree with
  upstream; zero count deltas. All **1,254,350 BAM records** exactly agree as a
  byte-record multiset, including scientific tags and multiplicity. All **937**
  BAM reference definitions agree. Header set differences are limited to `@PG`
  program metadata and `@CO user command line` binary/output paths.
  `official-vs-fixed-bam-audit.json` and `official-vs-fixed-counts.json` retain
  the result; this is unsorted BAM, not indexed-retrieval qualification.
- Second 8-thread candidate also passes against upstream; all declared raw
  signatures match exactly. Its 1,254,350 BAM records match the first corrected
  candidate as a multiset; all 12 Solo axis TSV files match.
- Extended first-candidate audit: all 12 raw/filtered/EM matrix numeric coordinate
  maps match upstream exactly (Decimal parsing for emitted real-valued EM entries,
  no relaxed tolerance); all 12 raw/filtered axis TSV files and all 28 non-timing
  Log.final.out fields match. This is the
  observed prefix's cell calls, not proof of representative full-library calling.
- Single-thread uninstrumented vs eight-thread corrected candidate: all five raw
  integer matrices/axes have zero count differences. Evidence:
  `real-tmj-profile-20261006/threads8-vs-threads1-counts.json`.
- Final single-thread miniature integration passed at
  `/tmp/star-cpu-regression-h7vdfpm9`, including mandatory three-layer output.

The old baseline result is retained with its failed comparison. It is not silently
regenerated to match the new algorithm. The first independently checked corrected
candidate becomes an eligible oracle only for its validated profile.

## Harness stability and automation

Run reuse requires content signatures, matching arguments, completed producer,
unchanged producer binary and output hashes. Missing records reject reuse.
Matrix dimensions/nnz/coordinates/axis lengths are validated; integer values are
compared exactly. Coordinate line order alone is separately reported, not confused
with a scientific difference. The diagnostic count reporter does not itself return
a failure merely because counts differ; the benchmark runner owns the gate.
Runner and comparator snapshots are retained. Old records lacking these stronger
receipts are unverified for automatic reuse, not retroactively certified.

Linux CI now runs the failure-gate tests and miniature integration; all platform
CTest invocations fail on zero discovered tests. Remote CI has not been run here.
Default libdeflate remains OFF. The former libdeflate real run only qualified the
pre-clipping-fix source; do not relabel that binary as the current source.

## Profiling and experimental boundary

Single-thread uninstrumented/instrumented runs:
`/home/ubuntu/star-cross-validation/real-tmj-profile-20261006`.
The instrumented run preserves `gmon.out` under its own output directory. It is for
qualitative hotspot localization, not multicore weight or speedup claims. Index
load/storage/cache and concurrent validation make wall-time comparisons confounded.

Both single-thread runs completed and the declared integer matrices/axes/SJ match
exactly. Instrumented build also passes 80/80 unit cases. GNU time reports 192.46s
unprofiled / 196.28s instrumented, user CPU 79.33s / 101.52s; max RSS approximately
29.25 GiB. Earlier runner `wall_seconds` actually used Python monotonic elapsed,
185.64s / 189.69s. These clocks disagree here; no cause or speedup is inferred.
The current runner now labels monotonic elapsed separately and reads wall elapsed
from GNU time. Late status/axis/timer hardening passed the local mock failure suite;
real runs retain their actual runner snapshot rather than being relabeled current.

`gprof -b -p profile-build/STAR real-tmj-profile-20261006/profiled/gmon.out`
reported 0.01s sample intervals. Leading self-time entries:

| Function | Sampled self seconds | Whole-profile self fraction |
|---|---:|---:|
| sjdbBuildIndex | 17.28 | 30.03% |
| compareSeqToGenome | 10.93 | 18.99% |
| PackedArray::writePacked | 10.28 | 17.86% |
| Parasail scan adapter clipping | 2.42 | 4.21% |
| ReadAlign::stitchPieces | 1.55 | 2.69% |
| ReadAlign::maxMappableLength2strands | 1.48 | 2.57% |

ReadAlignChunk::processChunks has 25.41s attributed inclusive sampled CPU time.
These are instrumented whole-pipeline weights, not direct multicore speedup
predictions. The profiled stage timestamps are index load 77s, GTF/junction prep
approximately 78s, mapping approximately 38s, then short Solo counting. Full-data
and warm resident-index workloads have different cost proportions. UMI collapse
is not a leading hotspot on this prefix; do not start there based on habit.

Recommended **first isolated GPU experiment**: retain the exact packed index and
batch `maxMappableLength`/`compareSeqToGenome` seed searches over a resident index.
Keep read partitioning, prefix/SA bounds, candidate enumeration, stitching, UMI,
EM and EmptyDrops on CPU initially. Freeze read and complementary-read bytes,
S/N/L, SA interval, read/genome strand, packed widths/masks, N/spacer/sentinel
semantics and effective index generation. Compare mapped length, full returned SA
range and multiplicity exactly; `compRes` is defined on a mismatch only and must
not be compared as an initialized semantic output on an exact match.
Start with the CPU field-level fixture/corruption comparator, then one synchronous
CUDA adapter; default OFF. Measure cold upload versus warm index reuse, packing,
transfer, kernel and gathering separately. No module benefit is yet established.

The RTX PRO 6000 Blackwell Max-Q reports 97,887 MiB total / 95,057 MiB free VRAM
at inspection. `nvcc` is absent. CPU tests do not qualify a CUDA toolchain.
Start only a bounded experimental module after inspecting profile evidence; CUDA
must remain optional/OFF, CPU path remains the oracle, and no success may hide a
different scientific result behind fallback or relaxed integer tolerances.

## Preserved work / remaining scope

The pre-existing seven added lines in ParametersSolo.cpp are preserved; profiles
explicitly request Gene together with Velocyto, so automatic insertion is inactive.
No source data, production outputs or previous results were deleted or overwritten.
No new commit/push is claimed. Full-library cell filtering, all chimeric/legacy
variants, other chemistries, fresh Windows/macOS builds and actual GPU kernels remain
separate qualification work, not implied by this engineering readiness gate.
