## Reference
`workflow/260412-release-validation-test.md` - Release validation: smoke test (1M reads) + full test (434M reads) procedures

## Completed (Implementation Done)
`workflow/done/260629-star-cross-v0.0.1-release.md` - STAR-Cross 0.0.1 release: rebrand+main branch, release automation (binaries v0.0.1), CodeQL httplib fix, WASP vW-in-SAM (#2617), multicore genomeGenerate (#2687, index-equivalence gated) (PR #8)
`workflow/done/260629-cross-platform-cram-bigendian.md` - Referenceless CRAM output, macOS readFilesCommand (posix_spawn), big-endian support (guarded), Makefile ARM64 parity (PR #7, CI green)
`workflow/done/260414-star-webui-llamacpp-stack.md` - WebUI: all 4 phases complete (server, job runner, UI, STARsolo artifacts, hardening)
`workflow/done/260413-unit-test-strategy.md` - Unit tests: doctest harness + PackedArray, binarySearch2, FastResetVector, UMI graph, blocksOverlap, EmptyDrops, directed collapse
`workflow/done/260413-algorithmic-optimizations-retry.md` - Algo optimization retry: harness script (validate_build.sh) + regression checks; core optimizations already in codebase
`workflow/done/260413-ggpeti-chimscore-cherry-pick-plan.md` - ggPeti chimeric bugfixes: block overlap, exon selection, trimming, roStart fix (Phase 1 implemented; regression fixtures and optional feature pending)
`workflow/done/260414-large-file-breakdown-plan.md` - Parameters.cpp split: 1286-line file broken into 5 focused files (all ≤515 lines)
`workflow/done/260413-dependency-minimization.md` - Dependency minimization: Opal→Parasail, optional system HTSlib, dependency inventory
`workflow/done/260412-windows-port.md` - Windows MSVC native build support
`workflow/done/260412-windows-perf-bottleneck.md` - Windows perf: 206 → 518 M/hr (2.5x improvement, Phases 1-5)
`workflow/done/260412-cpp-best-practices.md` - C++17, CI/CD, clang-tidy, Docker, CTest (mostly complete, 3 items deferred)
`workflow/done/260412-htslib-upgrade.md` - HTSlib 1.3 → 1.21 upgrade complete
`workflow/done/260413-algorithmic-optimizations.md` - Algorithm: FastResetVector, safe early rejection, EmptyDrops binary search + on-demand memory, Union-Find UMI
`workflow/done/260413-dependency-upgrades.md` - Dependency upgrades: Opal→Parasail, SimpleGoodTuring cleanup (zlib already at 1.3.2)
`workflow/done/260413-intel-oneapi-integration.md` - Intel ICX compiler for OpenMP 5.x (benchmarked; no throughput gain over MSVC)
`workflow/done/260413-devops-improvements.md` - DevOps: release automation, CI testing, Docker, CodeQL, pre-commit hooks
`workflow/done/260413-upstream-pr-cherry-picks.md` - Upstream PRs: 3 bug fixes (#2163, #535, #2676) + 2 perf (#791, #773)
`workflow/done/260413-project-hygiene-improvements.md` - Project hygiene: test data bundling, validation scripts, doc consolidation
`workflow/done/260413-starsolo-qc-report.md` - STARsolo QC report generator: HTML report from Solo.out (no BAM needed)
`workflow/done/260413-two-branch-strategy.md` - Two-variant strategy: STAR (compatible) + STARfast (all optimizations) via compile flag

## Plans (Approved, Awaiting Execution)
`workflow/done/260413-memory-management-fixes.md` - Memory management: 17 leaks, 5 missing destructors, stream factory refactor (awaiting ASAN validation setup)
`workflow/done/260413-winbindirty-debugging.md` - winBinDirty crash investigation: replace dirty-index tracker with upstream incarnation-based FastResetVector (awaiting reproduction on stress workload)
`workflow/done/260413-report-exporter-enhancements.md` - Report exporter enhancements: run context, feature-assignment breakdown, conditional sections (future work)

## Technical Notes
- STAR uses `pubsetbuf` on `istringstream` to use external buffers; MSVC ignores this. Fixed with `FixedStreamBuf.h`.
- Upstream STAR has 3 uninitialized `pthread_mutex_t` members that happen to work on Linux (zero-init = valid). Crashes on Windows.
- STAR's VLAs and `__uint128_t` usage requires platform-specific replacements on MSVC.
- EM multi-mapper probabilities (`UniqueAndMult-EM.mtx`) may differ by <0.001% between MSVC and GCC due to floating-point operation ordering. All integer count matrices are byte-identical.
- Parasail `test_verify` uses `${CMAKE_SOURCE_DIR}/data/test_small_2.fasta` which resolves to the STAR source root (not parasail's own dir) under FetchContent. Fixed by `file(COPY)` at configure time; `source/data/` is gitignored.
- WebUI embedded HTML raw strings split into 5 literals to avoid MSVC C2026 (65535-char token limit). HTML is in `source/webui/WebUI_html.inc`.
- WebUI job state is persisted to `{outDir}/.webui_jobs.jsonl` and restored on server restart (only if outputPrefix dir still exists).
- Endian-safe access to the genome/SA/packed-array byte stream goes through `source/byteOrder.h` (`loadUintLE`/`storeUintLE`, guarded by `STAR_BIG_ENDIAN`). LE keeps the native single-instruction load; only big-endian compiles take the portable byte-wise path. Force the portable path for testing with `-DSTAR_BIG_ENDIAN=1`.
- CRAM output (`--outSAMtype CRAM`) is implemented as a post-hoc BAM→referenceless-CRAM transcode in `source/cramOutput.cpp` (HTSlib `CRAM_OPT_NO_REF`), reusing the proven BAM path; it does not touch the hot per-record output. Transcriptome BAM stays BAM.
- `readFilesCommand` is spawned via `posix_spawnp` on POSIX (macOS-safe; upstream #2663) and `system()` on Windows, in `Parameters_openReadsFiles.cpp`.
