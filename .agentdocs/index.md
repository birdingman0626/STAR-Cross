## Current Task Documentation
`workflow/260412-release-validation-test.md` - Release validation test plan (reference)
`workflow/260413-dependency-upgrades.md` - Dependency upgrades: zlib 1.3.2, Opal→Parasail, SIMDe removal
`workflow/260413-intel-oneapi-integration.md` - Intel ICX compiler integration for OpenMP 5.x and better codegen
`workflow/260413-devops-improvements.md` - DevOps maturity plan: release automation, CI testing, Docker, CodeQL

## Completed
`workflow/done/260412-windows-port.md` - Windows MSVC native build support
`workflow/done/260412-windows-perf-bottleneck.md` - Windows perf: 206 → 518 M/hr (2.5x improvement)
`workflow/done/260412-cpp-best-practices.md` - C++17, CI/CD, clang-tidy, Docker, CTest
`workflow/done/260412-htslib-upgrade.md` - HTSlib 1.3 → 1.21 (evaluated, deferred, then completed)
`workflow/done/260413-upstream-pr-cherry-picks.md` - Upstream PR cherry-picks (bug fixes + performance)
`workflow/done/260413-algorithmic-optimizations.md` - Algorithm optimizations (Union-Find, EmptyDrops binary search)

## Technical Notes
- STAR uses `pubsetbuf` on `istringstream` to use external buffers; MSVC ignores this. Fixed with `FixedStreamBuf.h`.
- Upstream STAR has 3 uninitialized `pthread_mutex_t` members that happen to work on Linux (zero-init = valid). Crashes on Windows.
- STAR's VLAs and `__uint128_t` usage requires platform-specific replacements on MSVC.
- EM multi-mapper probabilities (`UniqueAndMult-EM.mtx`) may differ by <0.001% between MSVC and GCC due to floating-point operation ordering. All integer count matrices are byte-identical.
