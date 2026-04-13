## Current Task Documentation
`workflow/260412-htslib-upgrade.md` - HTSlib 1.3 → 1.21 upgrade plan (deferred - current version works correctly)
`workflow/260412-release-validation-test.md` - Release validation test plan (reference)
`workflow/260413-upstream-pr-cherry-picks.md` - Upstream PR cherry-pick plan: 6 PRs worth adopting
`workflow/260413-algorithmic-optimizations.md` - Algorithm-level optimizations: O(2^n)→DP, O(n²)→O(nL), cache prefetch

## Completed
`workflow/done/260412-windows-port.md` - Windows MSVC native build support
`workflow/done/260412-windows-perf-bottleneck.md` - Windows perf: 206 → 518 M/hr (2.5x improvement)
`workflow/done/260412-cpp-best-practices.md` - C++17, CI/CD, clang-tidy, Docker, CTest

## Technical Notes
- STAR uses `pubsetbuf` on `istringstream` to use external buffers; MSVC ignores this. Fixed with `FixedStreamBuf.h`.
- Upstream STAR has 3 uninitialized `pthread_mutex_t` members that happen to work on Linux (zero-init = valid). Crashes on Windows.
- STAR's VLAs and `__uint128_t` usage requires platform-specific replacements on MSVC.
- EM multi-mapper probabilities (`UniqueAndMult-EM.mtx`) may differ by <0.001% between MSVC and GCC due to floating-point operation ordering. All integer count matrices are byte-identical.
