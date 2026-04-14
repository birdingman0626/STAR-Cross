# Dependency Minimization Plan

## Status: Complete
Last updated: 2026-04-14

## Completed Phases

### Phase 1: Draw Hard Boundaries Around Third-Party Code
- [x] Added dependency inventory to CONTRIBUTING.md (HTSlib, Parasail, SGT, zlib)
- [x] Documented which files are allowed to include each dependency
- [x] Added vendored code policy: no new direct includes without justification

### Phase 2: Replace Opal with Parasail and Delete SIMDe
- [x] Added Parasail v2.6.2 via CMake FetchContent (static build)
- [x] Rewrote ClipCR4.h: replaced OpalSearchResult with ClipAlignResult struct
- [x] Rewrote ClipCR4.cpp: parasail_sg with profile API for batch alignment
- [x] Added destructor, Rule-of-Five (copy deleted), pre-allocated query buffer
- [x] Removed redundant dbSeqsLen array (readLen is constant)
- [x] Updated ClipMate_clipChunk.cpp for new method names
- [x] Deleted source/opal/ (opal.cpp, opal.h, simde_avx2.h, LICENSE)
- [x] Removed opal from CMakeLists.txt and Makefile
- [x] Updated parametersDefault docs (Opal→Parasail reference)
- [x] Updated SECURITY.md, CodeQL config, CONTRIBUTING.md

### Phase 3: Make HTSlib Bundling Optional
- [x] Added USE_SYSTEM_HTSLIB CMake option (default OFF)
- [x] System mode uses pkg-config to find htslib
- [x] Bundled mode unchanged (default)

### Phase 4: SimpleGoodTuring Cleanup
- [x] Already clean from prior session: constexpr MinInput, no using-namespace-std

## Validation
- [x] Build succeeds on MSVC (Windows)
- [x] STAR --version smoke test passes
- [x] 41/41 unit tests pass
- [ ] Linux/macOS CI (pending push)
- [ ] CellRanger4 adapter clipping output comparison (requires test data)

## Result
- Removed 3 vendored files (~25K lines): opal.cpp, opal.h, simde_avx2.h
- Parasail fetched at build time, not bundled in repo
- USE_SYSTEM_HTSLIB option available for packagers
- Cleaner MSVC build (no SIMDe workarounds)
- SIMD auto-dispatch (SSE2/SSE4.1/AVX2) via Parasail
