# Experimental CUDA junction-index remap

CUDA is optional and **OFF by default**. This is one bounded index-preparation
experiment, not a GPU implementation of read alignment or a Parabricks migration.
The CPU algorithm remains the oracle. No end-to-end speedup is promised.

The first 1M-pair Cyno engineering-prefix comparison matched the independent
official STAR CPU output: complete BAM records, raw/filtered/EM matrices, axes,
junctions and scientific statistics. One sequential measured pair took **102.82s
CPU vs 108.85s CUDA**. The CUDA kernel took about 0.063s but upload/download about
7.19s. This is a correctness-qualified scoped experiment, **not a performance
promotion** or a repeated/full-library benchmark. CPU identity copy is preferred
for this unchanged-index case; the next candidate is resident-index seed search.

## Build and select the backend

The host and device language levels are independent:
`-DSTAR_CXX_STANDARD=20 -DSTAR_CUDA_STANDARD=17` and
`-DSTAR_CXX_STANDARD=20 -DSTAR_CUDA_STANDARD=20` are distinct qualified profiles,
not interchangeable compiler flags. Defaults remain host17/device17. Current
dependency/toolchain execution is tracked in
[QUALIFICATION_RESULTS_20261007.md](QUALIFICATION_RESULTS_20261007.md).

CUDA builds require CMake >=3.24 and a compatible toolkit/device. CPU builds do
not discover or link CUDA. Linux/WSL example:

```sh
cmake -S source -B build-cuda -DCMAKE_BUILD_TYPE=Release -DSTAR_ENABLE_CUDA=ON -DSTAR_CUDA_ARCHITECTURES=120 -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc
cmake --build build-cuda --parallel 8
ctest --test-dir build-cuda/test --no-tests=error --output-on-failure
```

Architecture 120 is the tested Blackwell device, not a portable device default;
select your supported architecture explicitly. The default is `native`. CUDA 13.0
and GCC 13.3 were tested on WSL Ubuntu. Native Windows MSVC 19.51 + CUDA 13.3 also
builds and passes 82 CPU / 82 CUDA cases and the scoped 1M-pair CPU/CUDA comparison.
Use a Visual Studio Developer shell for native CMake/Ninja, not WSL; select the
Windows nvcc compiler. Other combinations remain unverified. The Make frontend
now delegates to CMake; CUDA is off by default and can be selected through
`CMAKE_ARGS`. These earlier GPU passes do not qualify the current dependency and
shared-header updates; follow [Q2 in the qualification plan](QUALIFICATION_PLAN.md).

Add `--gpuSjdbRemap required` to a STAR alignment command that triggers junction
preparation with the same GTF/index geometry. Modes:

- `off`: CPU; unchanged junction ordering takes an overlap-safe identity copy.
- `auto`: supported requests use CUDA; unavailable device, allocation capacity or
  unsupported geometry fall back before GPU output commitment, with a logged reason.
- `required`: unsupported/unavailable remap fails instead of qualifying CPU output
  as GPU execution. If the command never invokes junction preparation, this flag
  alone does not prove GPU execution; require a successful backend event.

Runtime transfer/kernel errors are fatal in both GPU modes, even after a downloaded
chunk. They are not silently retried as CPU success. All other stages remain CPU.

## Supported domain and cost

The adapter preserves packed suffix-array order/values while remapping existing
junction offsets. It supports **no new suffixes, no new junctions and no changed
genome/packed-index geometry**. Existing junction permutations are covered by unit
tests; new-junction and two-pass index mutation are not accelerated.

The complete packed SA is uploaded before any output is downloaded because STAR's
host buffers can overlap. Output uses bounded 128 MiB chunks, with one GPU thread
owning each output word; it never races through shared packed read-modify-write.
Device memory needs SA bytes plus the output chunk, junction mapping and CUDA
overhead. Insufficient capacity is a preflight refusal, not oversubscription.

`Log.out` records `GPU_SJDB_REMAP status=0` only on completion, together with upload,
kernel and download seconds, buffer bytes and chunk count. Status 1 is unavailable;
status 2 is failed. Host stage timers include synchronization but exclude allocation
and cleanup: use **whole-job wall time** for performance decisions, not their sum or
kernel time alone. CPU identity-copy events are logged separately.

## Reproduce the checks

```sh
python3 scripts/test_gpu_sjdb_integration.py --cpu /absolute/frozen/STAR.cpu --cuda /absolute/frozen/STAR.cuda
compute-sanitizer --tool memcheck --error-exitcode 99 build-cuda/test/star_tests '--test-case=CUDA packed SA remap*'
python3 scripts/benchmark_cpu_subset.py --fixture /absolute/fixture --data-dir /absolute/data --output-dir /absolute/new-run --binary cpu=/absolute/frozen/STAR.cpu --binary cuda=/absolute/frozen/STAR.cuda --gpu-sjdb-remap cuda=required
```

Freeze binaries before starting; the runner rejects a binary changed during the
run. It also requires genuine GPU completion, exact raw integer matrices/axes/SJ
and native spliced/unspliced/ambiguous layers. Separately compare complete BAM
records/headers, filtered/EM matrices and scientific statistics before claiming
profile-level equivalence. See [CPU validation](CPU_VALIDATION.md).

Unit coverage includes both strands, junction permutations, five packed widths,
tail/chunk boundaries and overlapping host buffers. Integration covers missing
device, CPU-only build and new-junction required rejection/auto fallback. These do
not certify full libraries, every chemistry, mid-kernel fault recovery, other GPU
tiers or biological validity. Keep the experiment opt-in until transfer-inclusive,
repeated matched comparisons satisfy the performance gate.

## Resident seed-search experiment (separate executable)

CUDA builds with tests enabled also create `test/star_seed_benchmark` (`.exe` on
Windows). It uploads Genome + packed SA once, retains the index, batches extension
queries and downloads only length, SA interval and multiplicity. It calls the actual
production CPU search functions for exact field comparisons. It **does not** replace
STAR read scheduling, clipping, seeding or downstream alignment/counting.

```sh
python scripts/test_gpu_seed_integration.py --star build-cuda/STAR.exe --benchmark build-cuda/test/star_seed_benchmark.exe
python scripts/run_seed_experiment.py --benchmark build-cuda/test/star_seed_benchmark.exe --genome data/genome_cynomolgus --fastq data/fixtures/example/R2.fastq --output data/validation/new-seed-run --reads 100000 --batch 65536 --repeats 3 --cpu-threads 8
```

Use platform-appropriate executable paths. The runner freezes input/code/binary
provenance and separates startup/index upload from warm resident-index throughput.
Queries use untrimmed ACGT pieces with actual prefix-index bounds; prefix-only cases
are skipped and counted. The bounded read pool is 16 MiB; reduce reads for longer
inputs. The synthetic real STAR index includes both directions, N/spacer/junction
boundaries, repetitive and variable-length queries; memory diagnostics pass after
explicitly preserving the CPU's boundary sentinels.

The initial native 100K-read / 199,935-query, three-round test matches all fields
and shows about **2.64x median warm search speedup**, including query transfers.
The tested larger batch (262,144 queries) reached **4.89x** local median speedup;
all four returned fields remained exact across three alternating-order rounds.
However, index setup costs about **3.72s** versus ~0.04s CPU search for this pool.
This is not a single-job or whole-STAR speedup. Prefer CPU for unamortized small
jobs; production integration remains experimental and requires complete-output
validation on actual adaptive post-clipping queries.

## Official Parabricks external comparison

On 2026-10-06, Parabricks 4.7.1-1 was tested on WSL/Blackwell using 1M real
Cyno R2 reads, one-pass RNA alignment, sorted BAM and no duplicate marking.
Three paired rounds on the same compatible index gave **21.985s CPU STAR
2.7.2a median versus 50.937s official GPU median**. Single automatic and
automatic-plus-GPU-sort/write screenings took 81.703s and 63.316s respectively.
Complete normalized BAM records and junction tables matched, not just totals.

This is a small-workload external comparison, not a benchmark of native Windows
STARsolo/Velocity or a general claim about GPU performance. Container/index
loading, initialization and output costs are included; compatible-index building
is excluded. CPU BAM indexing separately cost about 0.19s. No GPU default was
enabled. Local commands, hashes, logs and comparison receipts are retained under
`data/validation/parabricks-20261006/`; images and source data are not committed.

A separate Nsight Systems 2026.1.3 CUDA-only trace retained identical scientific
outputs. It recorded 154,387 kernel executions totaling 0.859s and 2.267s merged
device-operation time. Pinned-host allocation APIs took 7.463s; kernel launches
and stream synchronizations took 1.314s and 2.322s respectively. These durations
overlap and are not an additive wall-time breakdown. The trace prioritizes buffer
reuse and launch/synchronization granularity over arithmetic tuning for this
workload. Unified Memory tracing was unavailable; combined OSRT capture stalled
and was rejected. Profile elapsed time (56.934s, including export) is not included
in benchmark medians. Trace, SQLite, diagnostics and verification are retained
in the local `profile-fixed-1m-r3` evidence subfolder.

The resident-seed adapter was also traced natively on Windows with the frozen
100K-read input and 199,935 extension queries. Four batch kernels (warmup plus
three rounds) took 0.337–0.351ms each; CPU field-level verification passed. Initial
index upload remains the dominant CUDA cost. This trace supports continued
resident-index experimentation, not whole-STAR promotion: actual post-clipping
adaptive queries and a per-read-order-preserving batched scheduler are still
needed. CPU rolling-prefix construction, preprocessing reuse and UMI neighbor
hashing are separate candidates; measured benefits remain unqualified.
