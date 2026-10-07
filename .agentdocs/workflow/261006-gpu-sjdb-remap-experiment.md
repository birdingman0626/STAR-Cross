# First GPU experiment: junction packed-SA remap

Follow-up: [native Windows / resident seed experiment](261006-native-windows-resident-seed-experiment.md)
implements the next isolated search adapter and verifies Windows CPU/CUDA directly.
The WSL measurements and initial decision below remain historical evidence.

Date: 2026-10-06. CPU prerequisite commit `54ecaed` is pushed to origin/main.
Implementation is experimental / default OFF; final real-run correctness passes
but performance promotion is DEFERRED. This is source implementation, not binary reverse engineering
or transplantation of NVIDIA proprietary code.

## Selection and alternatives

The prior single-thread profile measured `sjdbBuildIndex` at 17.28s sampled self
time (30.03%), plus `PackedArray::writePacked` at 10.28s (17.86%). These overlapping
stage attributions are not additive end-to-end wall fractions. Inspecting the
actual 1M-pair workload showed zero new suffixes, unchanged genome/SA geometry and
identity old-junction order, but the entire 24,037,708,616-byte SA was rewritten.

Per the user's request to start at the largest cost, first remove the unnecessary
identity transformation on CPU and compare a real CUDA remap with that baseline.
Do not compare only against an avoidably slow CPU loop. The prior seed-search
recommendation remains the next mapping candidate, not a claim that it was already
implemented. UMI/BGZF offload was not selected: they are not leading costs here.

## Minimal implementation and boundary

- CPU identity path uses `memmove`, not `memcpy`: STAR's reserved host SA buffers
  overlap. It copies logical packed bytes and normalizes trailing padding.
- One adapter/header, optional CUDA static library and CPU-only stub; no generic
  dispatcher, task queue, nvCOMP or new runtime dependency in CPU builds.
- CUDA retains the full immutable input SA on device before downloading anything;
  bounded 128 MiB output chunks, exclusive 8-byte output-word ownership.
- Existing forward/reverse junction permutation formula and packed ordering remain
  unchanged; the original CPU loop handles unsupported transformations.
- Supports zero new suffixes/junctions, unchanged genome/SA widths and sizes only.
  New-junction/two-pass insertion is CPU or required-mode refusal.
- `--gpuSjdbRemap off|auto|required`, default off. Auto permits logged preflight
  unavailability only. Transfer/kernel/context failures are fatal, not CPU success.
  `required` on a command without junction preparation does not prove execution;
  the benchmark receipt separately requires completed GPU backend events.
- Device allocation budget includes full input + bounded output + junction map;
  no UVM or index tiling. CUDA overhead is additional, not measured peak VRAM.

Public reproduction/support contract: [GPU experiment](../../docs/GPU_EXPERIMENT.md)
(repository path: `docs/GPU_EXPERIMENT.md`).

## Toolchain and frozen artifacts

WSL Ubuntu, GCC 13.3, CUDA **13.0.88**, `/usr/local/cuda/bin/nvcc`, architecture
120, RTX PRO 6000 Blackwell Max-Q, driver 596.72. CUDA existed outside PATH;
the previous readiness note's `nvcc absent` inference was incomplete. Docker and
Parabricks container execution remain unqualified.

CPU/CUDA builds respectively:
`/home/ubuntu/star-cross-validation/baseline` and
`/home/ubuntu/star-cross-validation/gpu-sjdb-build`.
Release, tests ON, libdeflate OFF. Frozen final binaries are separate from build
outputs under `/home/ubuntu/star-cross-validation/gpu-sjdb-final-binaries-20261006`:

- STAR.cpu SHA256 `75d8302b2961a6b8974ee422142a716b421107ce42e0565582fb3980cf4bc8dd`.
- STAR.cuda SHA256 `c68e283db609617518b768f0463c5f884d05b02e1b54b446636ccdac3dcc68aa`.
- Independent official STAR 2.7.11b SHA256
  `d66601ed3589534cba53f897350e0b473cd99f4411be30a2cc7f4796d9a36920`.

Build version text still identifies the configuration's prerequisite HEAD, not
this uncommitted diff. Binary hashes and the subsequent implementation commit are
the authoritative provenance. The unrelated seven-line ParametersSolo.cpp user
change is preserved and excluded from commits; explicit Gene + Velocyto in every
real profile makes that automatic-Gene insertion inactive.

## Tests executed

- CPU: 82/82 STAR CTest cases; CUDA: 82/82 STAR CTest cases. Root CTest also ran
  four dependency cases (86 total); do not conflate those with STAR cases.
- CUDA packed-remap: 150 assertions, five widths (9/17/33/40/56), six record counts,
  24-byte chunk edges, both strands, nonidentity permutations and overlapping host
  buffers; expected values use the original sequential transformation/PackedArray.
- Compute-sanitizer memcheck: 150 assertions passed, **0 errors**. This is memory
  diagnostic coverage, not racecheck/full-library/fault-recovery certification.
- Python unittest discovery: 7/7 methods, including refusal to qualify missing GPU
  completion events and existing input/provenance/corruption gates.
- Final miniature GPU integration: `/tmp/star-gpu-sjdb-g3wy3cv0/result.json`, PASS.
  Required genuinely executes CUDA and matches CPU BAM/SJ; hidden device and
  CPU-only build reject required; hidden-device auto matches CPU; new-junction
  required rejects, auto matches the CPU insertion result.
- Final miniature CPU integration: `/tmp/star-cpu-regression-ufzlcao8/result.json`.
  Single-thread repeat: `/tmp/star-cpu-regression-8f2gvxn1/result.json`, PASS.
  Its pre-clipping-fix reference is appropriate only for this tiny fixture; the
  independent official binary is the real 1M-pair scientific oracle.

No injected illegal-address/mid-kernel partial-download fault or forced allocation
failure has been qualified. Windows/macOS/other GPU tiers, full-library cell calls,
alternative chemistry/chimeric profiles remain unverified.

## Preserved failed/superseded attempts

`real-tmj-gpu-sjdb-remap-20261006`: interim CPU used overlap-unsafe memcpy and is
disqualified, despite matching raw artifacts. Its GPU process was interrupted
after verifying exact ownership; runner records FAILED_EXECUTION. Nothing deleted.

`real-tmj-gpu-sjdb-remap-20261006-r2`: CPU ran 103.81s; interim GPU kernel completed
but a stricter CUDA memory-preflight error policy was rebuilt into its producer
path during execution. The binary-change gate correctly reports FAILED_VALIDATION.
Neither its GPU results nor 107.34s timing qualify the final binary. The lesson is
to freeze separate binaries before starting, not to relax the provenance gate.

## Final real profile and decision

Final root: `/home/ubuntu/star-cross-validation/real-tmj-gpu-sjdb-remap-20261006-r3`.
Read-only synchronized 1M-pair prefix, eight threads, same content-bound Cyno index,
GTF, whitelist, CellRanger4, Gene/GeneFull_Ex50pAS/Velocyto, EM, EmptyDrops_CR and
unsorted BAM as the CPU qualification. CPU mode off; CUDA mode required. Official
reference reused only through its unchanged binary/input/artifact receipt.

Final runner: **PASSED_DECLARED_RAW_ARTIFACTS**, both exit 0, input/binary signatures
unchanged. CUDA `required` completed on-device (status 0), no CPU fallback.
Expanded receipt: `expanded_scientific_comparison.json`, PASS for both against
the independent official run:

- 12 raw/filtered/EM matrices numerically exact (Decimal, no float tolerance);
- 12 feature/barcode axis files byte-exact; SJ rows byte-exact;
- 1,254,350 complete BAM records exact as a multiset, including multiplicity/tags;
- 937 binary BAM reference definitions and all other header lines exact after
  explicitly excluding only @PG/@CO provenance text;
- 28 scientific Log.final fields exact; only time/throughput fields excluded.

| Final measurement | CPU identity-copy | CUDA required |
|---|---:|---:|
| GNU time whole-job wall seconds | 102.82 | 108.85 |
| Maximum host RSS KiB | 30,672,388 | 30,786,488 |
| Remap stage timestamp interval (rounded seconds) | 3 | 8 |

CUDA measured upload 3.20884s, kernel+synchronization 0.0629376s, download 3.97792s,
180 chunks, device buffer budget 24,172,833,228 bytes (not measured peak device RSS).
Allocation/cleanup are included in whole-job wall time but not these stage timers.
Python monotonic elapsed is separately 99.2481s CPU / 105.2574s CUDA and remains
distinct from GNU time wall; do not replace the declared clock after observing it.

**Decision: keep CUDA default OFF; retain the bounded adapter as an opt-in
correctness experiment, not a promoted acceleration.** This pair observes CUDA
6.03s / 5.86% slower than the optimized CPU. Transfer cost overwhelms a no-op
identity transformation. One sequential pair is not a general slowdown estimate
and cannot satisfy three paired measurements, alternating order, cold/warm cache
and >=10% median gain. No claim is made for nonidentity permutations' performance.

Next experiment: resident packed-index batched `compareSeqToGenome` /
`maxMappableLength` seed search, with CPU semantic-field fixtures first. It performs
actual search rather than replacing an identity copy. Freeze returned length,
full SA interval/multiplicity, strand/N/sentinel behavior and index generation;
keep stitching, candidate selection and Solo/EM/EmptyDrops CPU. Measure one-job
upload and reused resident-index cases separately. It is not yet implemented.
