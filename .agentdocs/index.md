## Reference
`workflow/260412-release-validation-test.md` - Release validation: smoke test (1M reads) + full test (434M reads) procedures

## Active Work
`workflow/260413-algorithmic-optimizations-retry.md` - Algorithmic optimizations: FastResetVector, safe early rejection, EmptyDrops, Union-Find
`workflow/260413-ggpeti-chimscore-cherry-pick-plan.md` - ggPeti chimeric bugfixes: block overlap, exon selection, trimming, roStart fix (Phase 1 done; Phase 2 regression tests pending)

## Plans (Approved, Awaiting Execution)
`workflow/260413-memory-management-fixes.md` - Memory management: 17 leaks, 5 missing destructors, stream factory refactor
`workflow/done/260413-two-branch-strategy.md` - Two-variant strategy: STAR (compatible) + STARfast (all optimizations) via compile flag
`workflow/done/260413-starsolo-qc-report.md` - STARsolo QC report generator: HTML report from Solo.out (no BAM needed)
`workflow/done/260413-dependency-upgrades.md` - Dependency upgrades: Opal→Parasail, SimpleGoodTuring cleanup (zlib already at 1.3.2)
`workflow/done/260413-intel-oneapi-integration.md` - Intel ICX compiler for OpenMP 5.x (target: 518→600+ M/hr)
`workflow/done/260413-devops-improvements.md` - DevOps: release automation, CI testing, Docker, CodeQL, pre-commit hooks
`workflow/done/260413-upstream-pr-cherry-picks.md` - Upstream PRs: 3 bug fixes (#2163, #535, #2676) + 2 perf (#791, #773)
`workflow/done/260413-algorithmic-optimizations.md` - Algorithm: O(2^n)→DP stitching, O(n²)→O(nL) UMI, EmptyDrops binary search
`workflow/done/260413-project-hygiene-improvements.md` - Project hygiene: test data bundling, validation scripts, doc consolidation

## Completed (Implementation Done)
`workflow/done/260413-dependency-minimization.md` - Dependency minimization: Opal→Parasail, optional system HTSlib, dependency inventory
`workflow/done/260412-windows-port.md` - Windows MSVC native build support
`workflow/done/260412-windows-perf-bottleneck.md` - Windows perf: 206 → 518 M/hr (2.5x improvement, Phases 1-5)
`workflow/done/260412-cpp-best-practices.md` - C++17, CI/CD, clang-tidy, Docker, CTest (mostly complete, 3 items deferred)
`workflow/done/260412-htslib-upgrade.md` - HTSlib 1.3 → 1.21 upgrade complete

## Technical Notes
- STAR uses `pubsetbuf` on `istringstream` to use external buffers; MSVC ignores this. Fixed with `FixedStreamBuf.h`.
- Upstream STAR has 3 uninitialized `pthread_mutex_t` members that happen to work on Linux (zero-init = valid). Crashes on Windows.
- STAR's VLAs and `__uint128_t` usage requires platform-specific replacements on MSVC.
- EM multi-mapper probabilities (`UniqueAndMult-EM.mtx`) may differ by <0.001% between MSVC and GCC due to floating-point operation ordering. All integer count matrices are byte-identical.
- Parasail `test_verify` uses `${CMAKE_SOURCE_DIR}/data/test_small_2.fasta` which resolves to the STAR source root (not parasail's own dir) under FetchContent. Fixed by `file(COPY)` at configure time; `source/data/` is gitignored.
