# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STAR-Cross is a cross-platform fork of STAR (Spliced Transcripts Alignment to a Reference), a high-performance RNA-seq aligner written in C++20 (C++20-only; C++17 builds were removed). It performs spliced alignment of short and long reads to a reference genome, supporting features like chimeric/fusion detection, single-cell (STARsolo), two-pass mapping, and transcriptome quantification. It builds on Linux (x86_64, aarch64, s390x), macOS and Windows (MSVC/clang-cl).

## Build Commands

CMake is the primary build system; see `docs/BUILD.md` for per-platform details. The first configure downloads pinned dependencies (hash-checked against `dependencies.lock.json`).

```bash
# Release build with unit tests (from the repository root)
cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTAR_BUILD_TESTS=ON
cmake --build build

# Presets (run from source/): release, debug, sanitized (ASan+UBSan)
cmake --preset release && cmake --build --preset release
```

Useful options: `-DSTAR_LONG_READS=ON` (STARlong), `-DSTAR_USE_AVX2=ON`, `-DSTAR_ASAN=ON`, `-DSTAR_UBSAN=ON`, `-DSTAR_TSAN=ON` (Clang + LLVM OpenMP), `-DSTAR_CAPTURE_SEEDS=ON` (diagnostics only, not for benchmarks), and on GCC/Clang `-DSTAR_LTO=ON` and `-DSTAR_PGO=GENERATE|USE` with `-DSTAR_PGO_PATH=` (opt-in; measure before adopting). A build directory still configured with `STAR_CXX_STANDARD=17` fails at configure time; use a fresh one.

`source/Makefile` is a thin wrapper around the same CMake build (`make STAR`, `make STARlong`).

## Testing

- Unit tests (doctest, `test/unit/`, plus seed-search property tests): `ctest --test-dir build --no-tests=error --output-on-failure -j4`
- Validation-harness self-tests: `python3 -m unittest discover -s scripts -p 'test_*py'`
- Miniature end-to-end alignment/count regression: `python3 scripts/test_cpu_upstream.py --star-exe build/STAR` (add `--ref-exe` to compare against a frozen baseline binary)
- Multi-threaded race and thread-count-invariance check (use a `-DSTAR_TSAN=ON` Clang build): `python3 scripts/test_thread_sanitizer.py --star-exe build/STAR --threads 8`
- Malformed-input robustness (use an ASan/UBSan build for memory-safety evidence): `python3 scripts/test_malformed_inputs.py --star-exe build/STAR`
- `.github/workflows/build.yml` is the authoritative list of CI gates (platform matrix, frozen-baseline index/alignment equivalence, GCC and Clang ASan/UBSan and ThreadSanitizer jobs). Clang's UBSan also reports pointer arithmetic with a wrapped unsigned offset (`p + (a - b)` where `a < b`); write such offsets as signed `int64`.

Scientific changes must preserve exact output parity (BAM records, SJ.out.tab, Solo matrices) against the frozen baseline; performance claims need paired, calibrated measurements (see `docs/QUALIFICATION_PLAN.md`).

## Architecture

### Entry Point and Execution Flow

`source/STAR.cpp` — `main()` function. Handles parameter parsing, genome generation vs. alignment mode dispatch, two-pass execution, and output finalization.

### Core Modules

- **Parameters** (`Parameters.h/.cpp`, `Parameters_*.cpp`, `ParametersSolo.cpp`) — Command-line parameter parsing and validation. Default parameters are embedded via `parametersDefault.xxd` (generated from `parametersDefault` text file by `xxd`).

- **Genome** (`Genome.h/.cpp`, `Genome_*.cpp`) — Genome index generation, loading (including shared memory), suffix array construction, splice junction database building.

- **ReadAlign** (`ReadAlign.h/.cpp`, `ReadAlign_*.cpp`) — Core alignment engine. Seed finding via suffix array, extension, stitching windows, chimeric detection, output formatting. This is the largest module with ~30 implementation files.

- **ReadAlignChunk** (`ReadAlignChunk.h/.cpp`) — Parallelization layer. Reads are split into chunks processed by OpenMP threads, each with its own ReadAlign instance.

- **Transcriptome** (`Transcriptome.h/.cpp`, `Transcriptome_*.cpp`) — Gene/transcript quantification, alignment classification against annotation.

- **STARsolo** (`Solo*.cpp`, `SoloFeature*.cpp`, `SoloRead*.cpp`, `SoloBarcode*.cpp`) — Single-cell RNA-seq processing: barcode extraction, UMI deduplication, cell filtering, count matrix output.

- **Chimeric Detection** (`ChimericDetection.*`, `ChimericAlign.*`, `ChimericSegment.*`) — Fusion/chimeric junction identification and output.

- **Splice Graph** (`SpliceGraph.*`) — Graph-based alignment with Smith-Waterman scoring for spliced reads.

- **GTF/Annotation** (`GTF.h/.cpp`, `GTF_*.cpp`, `SuperTranscriptome.*`) — Gene model parsing, super-transcript construction.

- **BAM/SAM I/O** (`BAMoutput.*`, `BAMfunctions.*`, `ReadAlign_alignBAM.cpp`, `samHeaders.cpp`) — Output generation, coordinate sorting, deduplication. Uses bundled HTSlib (`source/htslib/`).

### Key Design Patterns

- **File-per-method convention**: Large classes split methods across files named `ClassName_methodName.cpp` (e.g., `ReadAlign_mapOneRead.cpp`).
- **OpenMP threading**: Parallel read processing via `mapThreadsSpawn.cpp`; each thread gets its own `ReadAlignChunk`/`ReadAlign` instances.
- **Shared memory**: Optional genome preloading into POSIX shared memory for multi-instance sharing (`SharedMemory.*`).
- **Compile-time variants**: `COMPILE_FOR_LONG_READS` (STARlong), `COMPILE_FOR_MAC`, `POSIX_SHARED_MEM` controlled via `-D` flags.
- **SIMD**: AVX2 by default for Opal alignment (`opal/opal.cpp`), configurable via `CXXFLAGS_SIMD`.

### Dependencies

- **HTSlib** (bundled in `source/htslib/`, optional system via `USE_SYSTEM_HTSLIB`) — BAM/SAM/CRAM I/O
- **Parasail** (CMake FetchContent, v2.6.2) — sequence alignment for CellRanger4 adapter clipping (`ClipCR4`)
- **zlib** — compression (system library)
- **OpenMP** — thread parallelism (compiler built-in)
- **xxd** — build tool to embed `parametersDefault` into the binary

### Directory Layout

- `source/` — All C++ source, Makefile, bundled htslib
- `bin/` — Pre-compiled binaries for Linux and macOS
- `doc/` — STARmanual.pdf
- `docs/` — Additional docs (STARsolo.md, STARconsensus.md)
- `extras/` — Miscellaneous scripts and test utilities
