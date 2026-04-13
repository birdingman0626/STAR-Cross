STAR 2.7.11b (Community Fork)
==========
Spliced Transcripts Alignment to a Reference
© Alexander Dobin, 2009-2024
https://www.ncbi.nlm.nih.gov/pubmed/23104886

> **Fork Notice:** The upstream STAR repository (`alexdobin/STAR`) appears to be unmaintained as of 2025
> (see [community discussion](https://www.reddit.com/r/bioinformatics/comments/1joyd0p/the_star_aligner_is_unmaintained_now/)).
> This fork maintains full output compatibility with STAR 2.7.11b while adding **Windows native support**.
> All changes are validated to produce byte-identical results to the original 2.7.11b release.
> Development is Vibe Coding driven but output-correctness tested.

ORIGINAL AUTHOR
===============
Alex Dobin, dobin@cshl.edu </br>
https://github.com/alexdobin/STAR/issues </br>
https://groups.google.com/d/forum/rna-star

FORK MAINTAINER
===============
birdingman0626 </br>
https://github.com/birdingman0626/STAR/issues

HARDWARE/SOFTWARE REQUIREMENTS
==============================
  * x86-64 compatible processors
  * 64 bit Linux, Mac OS X, or Windows

MANUAL
======
https://github.com/alexdobin/STAR/blob/master/doc/STARmanual.pdf

[RELEASEnotes](https://github.com/alexdobin/STAR/blob/master/RELEASEnotes.md) contains detailed information about the latest major release
[CHANGES](https://github.com/alexdobin/STAR/blob/master/CHANGES.md) contains detailed information about all the changes in all releases

DIRECTORY CONTENTS
==================
  * source: all source files required for compilation
  * bin: pre-compiled executables for Linux and Mac OS X
  * doc: documentation
  * extras: miscellaneous files and scripts

COMPILING FROM SOURCE
=====================

Download the latest [release from](https://github.com/alexdobin/STAR/releases) and uncompress it
--------------------------------------------------------

```bash
# Get latest STAR source from releases
wget https://github.com/alexdobin/STAR/archive/2.7.11b.tar.gz
tar -xzf 2.7.11b.tar.gz
cd STAR-2.7.11b

# Alternatively, get STAR source using git
git clone https://github.com/alexdobin/STAR.git
```

Compile under Linux (Make)
--------------------------

```bash
# Compile
cd STAR/source
make STAR
```
For processors that do not support AVX extensions, specify the target SIMD architecture, e.g.
```
make STAR CXXFLAGS_SIMD=sse
```

Compile under Linux (CMake - new)
---------------------------------

```bash
cd STAR/source
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```


Compile under Mac OS X
----------------------

```bash
# 1. Install brew (http://brew.sh/)
# 2. Install gcc with brew:
$ brew install gcc
# 3. Build STAR:
# run 'make' in the source directory
# note that the path to c++ executable has to be adjusted to its current version
$cd source
$make STARforMacStatic CXX=/usr/local/Cellar/gcc/8.2.0/bin/g++-8
# 4. Make it availible through the terminal
$cp STAR /usr/local/bin
```

Compile under Windows (MSVC)
----------------------------

STAR can be built natively on Windows using Microsoft Visual C++ and CMake.

```bash
# 1. Open "x64 Native Tools Command Prompt for VS"
# 2. Navigate to STAR source directory
cd STAR\source

# 3. Configure and build with CMake (zlib is fetched automatically)
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cd build
nmake

# 4. The resulting STAR.exe is in the build directory
STAR.exe --version
```

Build options:
```bash
# Build STARlong variant for long reads
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DSTAR_LONG_READS=ON

# Disable AVX2 (for older processors)
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DSTAR_USE_AVX2=OFF
```

**Windows performance:** ~518 M reads/hr with 12 threads on MSVC (vs ~728 M/hr on Linux GCC). The 1.4x gap is primarily due to MSVC's OpenMP 2.0 limitation.

**Windows limitations:**
  * Shared memory genome loading (`--genomeLoad LoadAndKeep/Remove`) is not supported; only `--genomeLoad NoSharedMemory` (the default) is available
  * `--readFilesCommand` uses temporary files instead of FIFO pipes

**Output compatibility:** All integer count matrices (raw and filtered) are byte-identical to Linux STAR 2.7.11b. The EM multi-mapper probability files (`UniqueAndMult-EM.mtx`) may differ by <0.001% in the last decimal place due to cross-compiler floating-point operation ordering. This does not affect biological results.

All platforms - non-standard gcc
--------------------------------

If g++ compiler (true g++, not Clang sym-link) is not on the path, you will need to tell `make` where to find it:
```bash
cd source
make STARforMacStatic CXX=/path/to/gcc
```

If employing STAR only on a single machine or a homogeneously setup cluster, you may aim at helping the compiler to optimize in way that is tailored to your platform. The flags LDFLAGSextra and CXXFLAGSextra are appended to the default optimizations specified in source/Makefile.
```
# platform-specific optimization for gcc/g++
make CXXFLAGSextra=-march=native
# together with link-time optimization
make LDFLAGSextra=-flto CXXFLAGSextra="-flto -march=native"
```

FreeBSD ports
=============

STAR can be installed on FreeBSD via the FreeBSD ports system.
To install via the binary package, simply run:
```
pkg install star
```

LIMITATIONS
===========
This release was tested with the default parameters for human and mouse genomes.
Mammal genomes require at least 16GB of RAM, ideally 32GB.
Please contact the author for a list of recommended parameters for much larger or much smaller genomes.

FORK CHANGES
============

### Windows Native Support (new)
  * CMake build system (`source/CMakeLists.txt`) supporting MSVC, GCC, and Clang
  * Windows compatibility layer (`source/wincompat.h`) providing POSIX API shims
  * `FixedStreamBuf.h`: cross-platform replacement for `pubsetbuf` (no-op on MSVC)
  * Automatic zlib download via CMake FetchContent when system zlib is unavailable
  * All VLAs replaced with `std::vector` for C++ standard compliance
  * Missing `#include <numeric>` added for MSVC compatibility
  * Missing mutex initializations fixed (portability bug in upstream)
  * OpenMP loop variables changed to signed types (MSVC OpenMP 2.0 compliance)

### Performance Optimizations (Windows)
  * MSVC compiler: `/O2 /Ob2 /Oi /GL` with `/LTCG` link-time optimization
  * SRW locks replacing CRITICAL_SECTION (faster mutex)
  * 4MB ifstream read buffer for FASTQ input
  * Zero-allocation read header parsing (direct `char*` instead of `istringstream`)
  * Result: **2.5x speedup** (206 → 518 M reads/hr)

### Bug Fixes (applicable to all platforms)
  * Initialize all `pthread_mutex_t` members in `ThreadControl` (upstream only initialized 8 of 11)
  * Fix `stitchAlignToTranscript` declaration/definition `const` mismatch

### Project Quality
  * C++17 standard (upgraded from C++11)
  * GitHub Actions CI (Linux GCC/Clang, macOS, Windows MSVC)
  * Dockerfile for reproducible builds
  * `.clang-tidy`, `.clang-format`, `.editorconfig` configs
  * CTest integration
  * Makefile OBJECTS bug fix (7 entries had `.cpp` instead of `.o`)

### Evaluated and Rejected
  * **CUDA GPU acceleration**: Tested on RTX PRO 6000 Blackwell (96GB VRAM). STAR's bottleneck is memory-latent suffix array search, not parallelizable compute. GPU overhead exceeded the gains.
  * **Intel oneAPI/MKL/IPP**: STAR does no linear algebra or signal processing. The remaining 1.4x gap vs Linux is from MSVC's OpenMP 2.0 and code generation, not addressable by Intel libraries.
  * **HTSlib upgrade (1.3 → 1.21)**: Evaluated and deferred. Current 1.3 has minimal security surface (no CRAM, no network I/O, trusted input only). Upgrade is high-risk (8 years of API changes, new dependencies) with no functional benefit for current use cases.
  * **Branch-and-bound pruning in alignment stitching** (upstream PR #773/#791 Phase A): Prototype changed transcript scoring order, producing different alignment results (unique mapping count shifted from 843K to 845K on 1M reads). Reverted to preserve output compatibility with upstream STAR.
  * **stitchWindowAligns early rejection** (upstream PR #773 partial): Same issue — skipping Transcript copies before scoring changes which alignments win in multi-mapping resolution. Reverted for output compatibility.

FUNDING
=======
The development of STAR is supported by the National Human Genome Research Institute of
the National Institutes of Health under Award Number R01HG009318.
The content is solely the responsibility of the authors and does not necessarily represent the official views of the National Institutes of Health.
