# C/C++ Best Practice Improvements for STAR

## Overview

Audit of the STAR codebase against modern C/C++ project best practices. Findings organized by category with priority and effort estimates.

---

## 1. Build System

### 1.1 Compiler Warnings — Priority: High, Effort: Low

**Current state:**
- GCC/Clang: `-Wall -Wextra` enabled (good)
- MSVC: `/W3` (moderate, not maximum)
- No `-Werror` anywhere — warnings don't break the build

**Improvements:**
- [ ] Add `-Werror` (GCC/Clang) and `/WX` (MSVC) to treat warnings as errors
- [ ] Raise MSVC warning level from `/W3` to `/W4`
- [ ] Suppress specific noisy warnings explicitly rather than lowering the overall level

### 1.2 Sanitizer Coverage — Priority: Medium, Effort: Low

**Current state:**
- AddressSanitizer supported via `STAR_ASAN` option, but MSVC-only (`/fsanitize=address`)
- No UBSan, ThreadSanitizer, or LeakSanitizer support

**Improvements:**
- [ ] Add GCC/Clang ASAN support (`-fsanitize=address`)
- [ ] Add UBSan option (`-fsanitize=undefined`) — catches integer overflow, null deref, alignment issues
- [ ] Add ThreadSanitizer option (`-fsanitize=thread`) — STAR uses OpenMP threading, data races are a real risk

### 1.3 CMake Enhancements — Priority: Low, Effort: Low

**Current state:**
- CMake 3.18 minimum, good use of target-scoped commands, no global flag pollution
- Missing several modern CMake conveniences

**Improvements:**
- [ ] Add `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` for LSP/IDE support (clangd, ccls)
- [ ] Add `CMakePresets.json` to standardize common build configurations (Release, Debug, ASAN, CUDA)
- [ ] Set explicit cmake_policy() for features used (e.g., CMP0077 for FetchContent option precedence)

### 1.4 Makefile OBJECTS List Bug — Priority: Medium, Effort: Low

**Current state:** 7 entries in the OBJECTS list use `.cpp` extension instead of `.o`:
- `Transcriptome_alignExonOverlap.cpp`
- `ReadAlign_calcCIGAR.cpp`
- `ChimericSegment.cpp`
- `ChimericAlign.cpp`
- `Parameters_openReadsFiles.cpp`
- `Parameters_closeReadsFiles.cpp`
- `GlobalVariables.cpp`

Works by accident (SOURCES wildcard catches them), but dependency tracking is wrong for these files.

- [ ] Fix all `.cpp` → `.o` in the OBJECTS list

---

## 2. Code Quality

### 2.1 C++ Standard — Priority: Medium, Effort: Medium

**Current state:** C++11 (2011 standard). The project compiles strictly to C++11.

**Improvements:**
- [ ] Upgrade to C++17 minimum — enables `std::optional`, `std::string_view`, `std::filesystem`, structured bindings, `if constexpr`
- [ ] Verify all target compilers support C++17 (GCC 7+, Clang 5+, MSVC 2017 15.7+) — all are widely available now

**Note:** This is a gradual change. Bump the standard first, then adopt features incrementally. No need to rewrite existing code immediately.

### 2.2 Memory Management — Priority: Medium, Effort: High

**Current state:**
- Heavy use of raw `new[]`/`delete[]` throughout (BAMoutput, ReadAlign, BAMbinSortByCoordinate, etc.)
- Near-zero smart pointer usage (one commented-out `unique_ptr` in ChimericAlign.h)
- STL containers (`vector`, `string`, `map`) used well alongside raw allocations

**Improvements:**
- [ ] For new code: mandate `std::unique_ptr` / `std::make_unique` instead of raw `new`
- [ ] Gradually migrate hot-path allocations in ReadAlign, BAMoutput to RAII wrappers
- [ ] Use `std::vector<char>` instead of `new char[N]` for dynamic buffers

**Risk note:** STAR is performance-critical. Smart pointer overhead is negligible for heap allocations, but measure before migrating inner-loop code.

### 2.3 Header Guard Consistency — Priority: Low, Effort: Low

**Current state:** All headers use `#ifndef` guards (good), but naming conventions vary: `H_Foo`, `DEF_Foo`, `CODE_Foo`, `INCLUDEDEFINE_DEF`.

- [ ] Standardize on one convention (e.g., `STAR_ClassName_H`) for new/modified headers

---

## 3. Tooling & CI

### 3.1 Static Analysis — Priority: High, Effort: Low

**Current state:** No static analysis integration whatsoever.

**Improvements:**
- [ ] Add `.clang-tidy` config with a conservative rule set (bugprone-*, performance-*, modernize-use-nullptr, readability-redundant-*)
- [ ] Integrate cppcheck into CI for a second opinion
- [ ] Run analysis on STAR source only, exclude htslib/opal (third-party)

### 3.2 CI/CD Modernization — Priority: High, Effort: Medium

**Current state:**
- Only `.travis.yml` present, building with GCC 4.9 (EOL 2019)
- No multi-platform testing, no CMake CI, no test execution

**Improvements:**
- [ ] Add GitHub Actions workflow with matrix: Linux (GCC, Clang) × macOS × Windows (MSVC)
- [ ] Test both Makefile and CMake build paths
- [ ] Include ASAN build in CI
- [ ] Run static analysis in CI (clang-tidy, cppcheck)
- [ ] Add a smoke test: genome generate + align on small test data, validate BAM output

### 3.3 Code Formatting — Priority: Medium, Effort: Low

**Current state:** No `.clang-format`, `.editorconfig`, or any formatting config.

**Improvements:**
- [ ] Add `.clang-format` based on existing code style (likely LLVM or Google with adjustments)
- [ ] Add `.editorconfig` for basic whitespace/encoding rules
- [ ] Format only new/modified code to avoid noisy diffs on legacy files

### 3.4 Testing Framework — Priority: Medium, Effort: High

**Current state:** No unit tests. Only manual validation by running STAR on test data. Empty `Test.hpp` skeleton exists.

**Improvements:**
- [ ] Integrate a lightweight framework (Catch2 single-header or GoogleTest via FetchContent)
- [ ] Add `enable_testing()` and `add_test()` in CMakeLists.txt
- [ ] Start with testable utility functions (SuffixArrayFuns, BAMfunctions, SoloBarcode matching) before tackling integration-level alignment tests
- [ ] Add integration smoke test as a CTest that runs STAR on bundled small test data

---

## 4. Project Structure

### 4.1 Source Directory Organization — Priority: Low, Effort: High

**Current state:** 249 files (159 .cpp + 90 .h) in a single flat `source/` directory with no functional grouping.

**Improvements (long-term):**
- [ ] Group into subdirectories by module: `align/`, `solo/`, `genome/`, `io/`, `chimeric/`, `transcriptome/`
- [ ] Move third-party code under `third_party/` (htslib, opal, SimpleGoodTuring)

**Risk note:** This is a large refactor that touches every `#include` and both build systems. Only worth doing if the project is actively maintained long-term. Not recommended as a near-term change.

### 4.2 .gitignore Gaps — Priority: Medium, Effort: Low

**Current state:** Minimal .gitignore. Build artifacts (build_asan/, build_cuda/, build_dbg/, etc.) are not ignored.

**Improvements:**
- [ ] Add CMake build directories: `build*/`, `_build/`
- [ ] Add binaries/libraries: `*.exe`, `*.pdb`, `*.a`, `*.lib`, `*.so`, `*.dll`
- [ ] Add CMake artifacts: `CMakeCache.txt`, `cmake_install.cmake`, `CMakeFiles/`
- [ ] Add generated files: `compile_commands.json`, `parametersDefault.xxd`
- [ ] Add editor artifacts: `*.swp`, `*~`, `.vscode/`, `.idea/`

### 4.3 Docker Support — Priority: Low, Effort: Low

**Current state:** No Dockerfile.

- [ ] Add a minimal Dockerfile for reproducible Linux builds and as a CI base image

---

## 5. Security (Core Code)

**Current state: Good.** Core STAR code avoids unsafe C functions (`sprintf`, `strcpy`, `gets`). Uses `std::string` and modern C++ I/O. Third-party htslib contains C-style patterns but is handled with suppressed warnings and `_CRT_SECURE_NO_WARNINGS`.

No action needed on security front.

---

## Priority Summary

| Item | Priority | Effort | Impact |
|---|---|---|---|
| Compiler warnings (`-Werror`, `/W4`) | High | Low | Catch bugs at compile time |
| Static analysis (clang-tidy) | High | Low | Catch bugs without runtime |
| CI modernization (GitHub Actions) | High | Medium | Multi-platform confidence |
| Makefile OBJECTS bug fix | Medium | Low | Correct dependency tracking |
| .gitignore gaps | Medium | Low | Clean repository |
| Code formatting config | Medium | Low | Consistent style |
| C++17 standard upgrade | Medium | Medium | Modern language features |
| Sanitizer coverage (UBSan, TSan) | Medium | Low | Runtime bug detection |
| Testing framework | Medium | High | Long-term quality |
| Memory management modernization | Medium | High | Safety, maintainability |
| Source directory reorganization | Low | High | Readability |
| CMake enhancements | Low | Low | Developer convenience |
| Docker support | Low | Low | Reproducible builds |
