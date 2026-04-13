# Windows CPU Read Processing Performance

## Status: Phases 1-5 Complete
Last updated: 2026-04-13

## Completed

- [x] Phase 1: Compiler flags — `/O2 /Ob2 /Oi /GL` + `/LTCG` link-time optimization
- [x] Phase 2: Thread stack — reduced from 64MB to 16MB per thread
- [x] Phase 3: SRW locks — replaced CRITICAL_SECTION with Slim Reader/Writer locks
- [x] Phase 4: ifstream buffer — 4MB read buffer via `pubsetbuf` on file streams
- [x] Phase 5: Eliminate per-read istringstream — replaced with `thread_local char[]` + direct `char*` parsing (strtoul/strtoull) in `readLoad.cpp`

## Deferred

- [ ] Phase 6: OpenMP alternatives — requires toolchain change (LLVM libomp or Intel compiler). Documented as limitation. MSVC ships OpenMP 2.0 only.

## Measurement

Baseline: 206 M/hr (before optimizations)
Target: 400+ M/hr
Needs full benchmarking run to measure actual improvement.
