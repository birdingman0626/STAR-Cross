# STAR-Cross v0.0.1 release + upstream PR sweep (done 2026-06-29)

Merged via PR #8 (squash `83e115b`). Release **v0.0.1** published with binaries for all 3 platforms. All CI green; 0 code-scanning alerts.

## Delivered
- **Rebrand/versioning**: repo renamed STAR-Win → STAR-Cross; default branch master → main; binary `--version` = `STAR-Cross 0.0.1_<hash>` (`CMakeLists.txt` + `source/VERSION`); `version_check` regex → `0.0.1`.
- **Releases reset**: deleted old releases (v2.7.11c, 2.7.11c_95fb67d) + their tags and the orphan `2.7.11c_913cac3` tag; tagged `v0.0.1` → `release.yml` built Linux x86_64 / macOS aarch64 / Windows x86_64 and attached binaries.
- **CodeQL httplib fix**: the only open alert (`cpp/non-https-url` in fetched `httplib.h`) resolved by marking cpp-httplib a SYSTEM include in CMake (CodeQL skips system headers; `paths-ignore` can't filter `#include`d C/C++ headers) + broadened config globs (`**/_deps/**`). Auto-closed on re-scan.
- **PR #2617** (WASP `vW:i` in SAM): emit vW in SAM/CRAM path; dropped BAM-only restriction (scoped to vW, unlike the upstream patch).
- **PR #2687** (multicore genomeGenerate SA build): ported; reconciled `funCompareSuffixesFromWord` with big-endian `loadUintLE`; `sjdbSortBucket` reformulated without `__uint128` (monotonic mapping → identical output); OpenMP loops switched to signed `int64` (MSVC OpenMP 2.0). **Gated by `extras/tests/scripts/validate_genome_equivalence.sh`** (CI job `validate-genome-index`): byte-identical SA/SAindex/Genome vs main baseline across 1/16 threads + low-RAM multi-chunk.

## Verified already-present (not re-done)
- #2163 (OOB write) removed in `sjdbInsertJunctions.cpp`; #2676 (SJ leak) in `outputSJ.cpp`; #2605 covered (macOS ARM via CMake + #2693 posix_spawn; opal Makefile bit moot).

## Notes / limitations
- Big-endian remains compile-only validated (no BE CI runner).
- Makefile path is not CI-tested (CI uses CMake).
- `release.yml` macOS artifact is aarch64 only; no Intel-mac binary.
