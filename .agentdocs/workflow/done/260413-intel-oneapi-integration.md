# Intel oneAPI Integration Plan

## Status: Pending
Last updated: 2026-04-13

## Goal

Use Intel ICX compiler (LLVM-based) to get OpenMP 5.x and better codegen for branch-heavy code, closing the remaining ~1.4x gap vs Linux GCC. Target: 518 M/hr → 600+ M/hr.

## Compatibility

ICX is LLVM-based — no Intel vendor lock-in, no AMD performance penalty. The built binary runs identically on Intel and AMD x86-64 CPUs. STAR does not use MKL/IPP (the Intel libraries that historically had vendor-specific code paths), so AMD compatibility is not a concern.

## Prerequisites

- [ ] Install Intel oneAPI Base Toolkit (or just the DPC++/C++ compiler component)
  - Download: https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html
  - Minimum install: select only "DPC++/C++ Compiler" (~2 GB instead of full 5+ GB)
  - Installs to `C:\Program Files (x86)\Intel\oneAPI\` by default

## Phase 1: CMake Integration (Effort: 2 hours)

### Add ICX as a compiler option in CMakeLists.txt

- [ ] Add a `STAR_USE_ICX` CMake option (OFF by default) to avoid breaking existing MSVC builds
- [ ] When ICX detected or requested, set compiler-specific flags:
  ```cmake
  if(CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
      # ICX uses Clang-compatible flags, not MSVC flags
      set(STAR_ICX ON)
  endif()
  ```

### Compiler flags for ICX

- [ ] Replace MSVC flags with ICX equivalents:
  ```cmake
  if(STAR_ICX)
      target_compile_options(STAR PRIVATE
          -O2 -fp-model=precise   # Match MSVC /O2 /fp:precise behavior
          -Qopenmp                # OpenMP 5.x (not MSVC's /openmp which is 2.0)
          -mavx2                  # AVX2 SIMD (same as -arch:AVX2)
          -Wall -Wextra           # Warnings (Clang-style)
      )
      target_link_options(STAR PRIVATE -Qopenmp)
  else()
      # Existing MSVC flags unchanged
  endif()
  ```

- [ ] LTO with ICX: add `-flto` (LLVM LTO, not MSVC /LTCG)
- [ ] Keep `/STACK:16777216` — ICX linker respects this

### Build command

```bash
# From oneAPI command prompt (setvars.bat sets up PATH):
cmake -B build_icx -G "NMake Makefiles" ^
  -DCMAKE_C_COMPILER=icx ^
  -DCMAKE_CXX_COMPILER=icx ^
  -DCMAKE_BUILD_TYPE=Release
nmake -C build_icx
```

Or with Ninja (faster parallel builds):
```bash
cmake -B build_icx -G Ninja ^
  -DCMAKE_C_COMPILER=icx ^
  -DCMAKE_CXX_COMPILER=icx ^
  -DCMAKE_BUILD_TYPE=Release
ninja -C build_icx
```

## Phase 2: OpenMP Tuning (Effort: 1 hour)

The main win from ICX. MSVC's OpenMP 2.0 has no thread affinity or advanced scheduling.

- [ ] Test with `OMP_PROC_BIND=close` and `OMP_PLACES=cores` environment variables
- [ ] Evaluate `schedule(dynamic)` vs `schedule(guided)` in hot OpenMP loops:
  - `source/bamSortByCoordinate.cpp` — BAM sort
  - `source/insertSeqSA.cpp` — SA insertion
  - `source/sjdbBuildIndex.cpp` — splice junction DB
- [ ] Try `#pragma omp parallel for simd` on vectorizable loops (if any qualify)

## Phase 3: Benchmark (Effort: 2 hours)

- [ ] Run smoke test (1M reads) with ICX build, compare wall time vs MSVC build
- [ ] Compare mapping stats (unique/multi/unmapped percentages) — must be identical
- [ ] Compare Solo.out count matrices — must be byte-identical to MSVC build
- [ ] If output matches, run full test (434M reads) for final throughput measurement

### Expected results

| Build | Expected M/hr | Improvement |
|---|---|---|
| MSVC /O2 + OpenMP 2.0 (current) | 518 | baseline |
| ICX -O2 + OpenMP 5.x | 570-620 | +10-20% |
| ICX -O2 -flto + OpenMP 5.x | 590-650 | +15-25% |

## Phase 4: CI Integration (Effort: 1 hour)

- [ ] Add ICX build variant to GitHub Actions (Intel provides a `intel/oneapi-ci` action)
- [ ] Add ICX CMake preset to `CMakePresets.json`

## Risks

- **htslib C code:** ICX compiles C differently than MSVC. Bundled htslib may produce warnings or need minor fixes.
- **OpenMP runtime:** ICX uses `libiomp5md.dll` (Intel's OpenMP runtime). This DLL must ship with the binary or be statically linked (`-Qopenmp-link=static`).
- **Floating-point:** ICX default fp-model may differ slightly from MSVC. Use `-fp-model=precise` to match.

## Decision: Static vs Dynamic OpenMP Linking

| Option | Pros | Cons |
|---|---|---|
| Dynamic (`libiomp5md.dll`) | Smaller binary | Must ship DLL with STAR.exe |
| Static (`-Qopenmp-link=static`) | Self-contained binary | ~2 MB larger |

**Recommendation:** Use `-Qopenmp-link=static` for distribution. Self-contained binary is simpler.
