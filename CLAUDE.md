# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STAR (Spliced Transcripts Alignment to a Reference) is a high-performance RNA-seq aligner written in C++11. It performs spliced alignment of short and long reads to a reference genome, supporting features like chimeric/fusion detection, single-cell (STARsolo), two-pass mapping, and transcriptome quantification.

## Build Commands

All builds run from the `source/` directory. HTSlib (bundled) is built automatically as a dependency.

```bash
cd source

# Standard Linux build
make STAR

# Static binary (no runtime deps)
make STARstatic

# Long reads variant
make STARlong

# Debug build (gdb, -O0 -g3)
make gdb

# macOS (adjust g++ path to your version)
make STARforMacStatic CXX=/usr/local/Cellar/gcc/8.2.0/bin/g++-8

# For CPUs without AVX2 (use sse or let compiler auto-detect)
make STAR CXXFLAGS_SIMD=sse

# Clean object files
make clean

# Deep clean (including htslib)
make CLEAN
```

Custom compiler/flags: `CXX=`, `CXXFLAGSextra=`, `LDFLAGSextra=`, `CXXFLAGS_SIMD=`.

## Testing

There is no formal unit test suite. Validation is done by running STAR on test data and checking outputs. Some AWK-based validation scripts exist in `extras/tests/scripts/` for STARsolo cell stats.

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
