# STAR-Cross contributor guidance

STAR-Cross is a C++20-only STAR derivative for Linux, macOS and Windows.
Preserve unrelated work, upstream attribution, scientific output semantics and
revision-scoped evidence. Documentation entry point: [docs/README.md](docs/README.md).

## Build and test

Use CMake; the Makefile delegates to it. Pinned dependencies are checked against
dependencies.lock.json. Platform/toolchain options: [docs/BUILD.md](docs/BUILD.md).

```sh
cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --no-tests=error --output-on-failure -j4
python3 -m unittest discover -s scripts -p 'test_*py'
python3 scripts/test_cpu_upstream.py --star-exe build/STAR
```

Use platform-appropriate executable paths. Add --ref-exe for a frozen baseline.
For memory safety use test_malformed_inputs.py with ASan/UBSan; for OpenMP races
use test_thread_sanitizer.py with Clang/LLVM OpenMP and STAR_TSAN=ON.
Local CI replay is documented in [docs/LOCAL_CI.md](docs/LOCAL_CI.md).
.github/workflows/build.yml owns hosted job definitions; do not claim unrun CI.

Host C++20 is mandatory; CUDA is optional and off by default. Its device
language option is separate. AVX2, LTO and PGO are opt-in. Measure their impact
before promotion; seed-capture builds are diagnostic, not performance builds.

## Code navigation

| Owner | Responsibility |
| --- | --- |
| source/STAR.cpp | Parameter parsing and run-mode dispatch |
| Parameters* / parametersDefault | Configuration, validation and embedded defaults |
| Genome* / SharedMemory* | Index generation/loading, junctions and shared memory |
| ReadAlign* | Seed search, windows/stitching, selection and alignment output |
| ReadAlignChunk* / mapThreadsSpawn.cpp | Per-worker chunks, OpenMP and input scheduling |
| Transcriptome* / GTF* | Annotation and transcript quantification |
| Solo* | Barcodes, UMI, features, filtering and matrices |
| Chimeric* / SpliceGraph* | Fusion detection and graph alignment |
| BAM* / samHeaders.cpp / source/htslib | Serialization, sorting and HTS I/O |

Large classes use Class_method.cpp files; inspect the relevant owner rather than
loading every implementation. Resource owners and borrowed views have distinct
lifetimes. Unsigned subtraction in pointer offsets can wrap; use signed offsets
where subtraction may be negative.

## Verification boundaries

Scientific changes compare full normalized BAM records/header/tags, SJ and all
declared Solo/Velocity outputs against a frozen oracle. A test pass or a kernel
speedup does not establish full-library equivalence or whole-job acceleration.
Performance and resource gates: [docs/QUALIFICATION_PLAN.md](docs/QUALIFICATION_PLAN.md).

For GPU work use [docs/GPU_FULL_PIPELINE_PLAN.md](docs/GPU_FULL_PIPELINE_PLAN.md).
Keep fallback, capacity, ordering and execution coverage explicit. Historical
measurements remain bound to their recorded source/binary/input identities.
