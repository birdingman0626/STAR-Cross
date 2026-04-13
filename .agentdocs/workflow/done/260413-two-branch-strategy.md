# Two-Variant Strategy: STAR (Compatible) + STARfast (Optimized)

## Status: Plan Complete — Awaiting Execution
Last updated: 2026-04-13

## Goal

Ship two binary variants from a single codebase: one guaranteed byte-identical to upstream STAR 2.7.11b, one with all performance optimizations. Users choose based on their needs — reproducibility vs speed.

---

## Architecture: Compile-Time Flag, Not Git Branches

| Approach | Maintenance | Risk |
|---|---|---|
| Two git branches | Every fix cherry-picked both ways; diverge over time | High — merge conflicts, forgotten backports |
| Compile-time flag (`-DSTAR_FAST_MODE`) | Single codebase, single CI, `#ifdef` guards | Low — STAR already does this for STARlong |

**Decision: Compile-time flag.** Follows the existing `COMPILE_FOR_LONG_READS` pattern.

```cmake
option(STAR_FAST_MODE "Enable aggressive optimizations (may change alignment output)" OFF)
```

---

## Optimization Classification

Every optimization must be classified before merging. The rule is simple: **does it change any output byte?**

### Tier 1: Always ON — Output-Identical (no flag needed)

These are unconditional. They go into both STAR and STARfast.

| Optimization | Source | File(s) | Why Output-Identical |
|---|---|---|---|
| Union-Find for connected components | Algorithmic plan #5 | `SoloFeature_collapseUMI_Graph.cpp` | Same components, different traversal order. UMI counts identical. |
| EmptyDrops binary search p-value | Algorithmic plan #6 | `SoloFeature_emptyDrops_CR.cpp` | `lower_bound` on sorted array ≡ linear scan count. Mathematically identical. |
| EmptyDrops on-demand memory | Algorithmic plan #7 | `SoloFeature_emptyDrops_CR.cpp` | Same values computed at needed points only. Numerically identical. |
| FastResetVector for winBin | Upstream PR #791 | `ReadAlign_stitchPieces.cpp`, `FastResetVector.h` | Same values read/written. Avoids memset, no algorithmic change. |
| PackedArray bitmask | Upstream PR #791 | `PackedArray.h` | `(a>>S) & mask` ≡ `((a>>S)<<wCL)>>wCL`. Same bits extracted. |
| UMI hash-based 1-mismatch | Algorithmic plan #4 | `SoloFeature_collapseUMI_Graph.cpp` | Enumerates same 1MM pairs via hash lookup vs sorted scan. |
| SA prefetching | Algorithmic plan #8 | `SuffixArrayFuns.cpp` | Prefetch hints don't change computation, only cache behavior. |
| Compiler flags (`/O2 /Ob2 /GL /LTCG`) | Perf plan Phase 1 | Build system | Codegen, not algorithmic. |
| SRW locks, ifstream buffer, istringstream removal | Perf plan Phases 2-5 | Various | I/O and threading, no algorithmic change. |
| Upstream bug fixes (#2163, #535, #2676) | Cherry-pick plan | Various | Bug fixes, not optimizations. |
| Opal → Parasail | Dependency plan | `ClipCR4.cpp` | Same SW algorithm, different SIMD implementation. Validate byte-identical. |

### Tier 2: Guarded by `STAR_FAST_MODE` — Output May Differ

These go into STARfast only. Each has a measured output delta.

| Optimization | Source | File(s) | What Changes | Measured Delta |
|---|---|---|---|---|
| Branch-and-bound pruning | Algorithmic plan #1A | `stitchWindowAligns.cpp` | Prunes branches that can't beat current best. Ties broken differently. | ~0.2% unique mapping shift (843K→845K on 434M reads) |
| Early rejection of overlapping aligns | Algorithmic plan #1A | `stitchWindowAligns.cpp` | Skips backward-overlapping alignments without scoring. | Part of same 0.2% |
| Upstream PR #773 early skip | Cherry-pick plan | `stitchWindowAligns.cpp` | Avoids Transcript copy when alignment fails. Changes scoring order. | TBD (author: 2.4x speedup) |
| DP stitching reformulation | Algorithmic plan #1B | `stitchWindowAligns.cpp` | O(n²) DP replaces O(2^n) enumeration. Different path when multiple optima exist. | TBD (~0.1-0.5% expected) |
| Batched SA queries | Algorithmic plan #8 | `ReadAlign_maxMappableLength2strands.cpp` | Interleaved binary search changes seed discovery order. May affect which seeds found first. | TBD (high risk, do last) |

### Classification Process for New Optimizations

```
New optimization proposed
    │
    ├── Run smoke test (1M reads): output identical? ──Yes──→ Tier 1 (always ON)
    │
    └── No → Run full test (434M reads): quantify delta
              │
              ├── Delta < 0.01% and no biological impact → Tier 1 (document exception)
              │
              └── Delta ≥ 0.01% or changes alignment selection → Tier 2 (guard with #ifdef)
```

---

## Source Code Pattern

```cpp
// stitchWindowAligns.cpp — only file needing #ifdef currently

    if (iA>=nA && tR2==0) return;

#ifdef STAR_FAST_MODE
    // Branch-and-bound pruning
    if (*nWinTr > 0 && iA < nA) {
        int maxRemaining = 0;
        for (uint ii = iA; ii < nA; ii++)
            maxRemaining += (int)WA[ii][WA_Length] * scoreMatch;
        if (Score + maxRemaining < wTr[0]->maxScore - (int)P.outFilterMultimapScoreRange)
            return;
    }
#endif

    if (iA>=nA) {
```

All Tier 1 optimizations go in without any guards — cleaner code, no `#ifdef` clutter.

---

## Build System

### CMakeLists.txt

```cmake
option(STAR_FAST_MODE "Aggressive optimizations (may change alignment output)" OFF)

if(STAR_FAST_MODE)
    target_compile_definitions(STAR PRIVATE STAR_FAST_MODE)
    set_target_properties(STAR PROPERTIES OUTPUT_NAME "STARfast")
    message(STATUS "  Fast mode: ON → binary: STARfast")
else()
    message(STATUS "  Fast mode: OFF → binary: STAR (upstream-compatible)")
endif()
```

### Makefile

```makefile
ifdef STAR_FAST_MODE
CXXFLAGSextra += -DSTAR_FAST_MODE
SFX := fast
endif
```

### Build Commands

```bash
# Compatible (default)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                              # → STAR / STAR.exe

# Fast
cmake -B build_fast -DCMAKE_BUILD_TYPE=Release -DSTAR_FAST_MODE=ON
cmake --build build_fast                         # → STARfast / STARfast.exe

# Makefile variants
make STAR                                        # → STAR
make STAR STAR_FAST_MODE=1                       # → STARfast
```

---

## Release Strategy

### GitHub Release Artifact Matrix

Each tagged release (`v*`) publishes **6 binaries** (2 variants × 3 platforms):

```yaml
# .github/workflows/release.yml
on:
  push:
    tags: ['v*']

jobs:
  build:
    strategy:
      matrix:
        include:
          # Compatible variants
          - os: ubuntu-latest
            variant: compatible
            fast_mode: OFF
            artifact: STAR-linux-x64
            build_cmd: make STARstatic
          - os: windows-latest
            variant: compatible
            fast_mode: OFF
            artifact: STAR-windows-x64.exe
            build_cmd: cmake --build build --config Release
          - os: macos-latest
            variant: compatible
            fast_mode: OFF
            artifact: STAR-macos-x64
            build_cmd: make STARforMacStatic

          # Fast variants
          - os: ubuntu-latest
            variant: fast
            fast_mode: ON
            artifact: STARfast-linux-x64
            build_cmd: make STARstatic STAR_FAST_MODE=1
          - os: windows-latest
            variant: fast
            fast_mode: ON
            artifact: STARfast-windows-x64.exe
            build_cmd: cmake --build build_fast --config Release
          - os: macos-latest
            variant: fast
            fast_mode: ON
            artifact: STARfast-macos-x64
            build_cmd: make STARforMacStatic STAR_FAST_MODE=1

  release:
    needs: build
    steps:
      - name: Create release
        run: |
          gh release create ${{ github.ref_name }} \
            --title "STAR ${{ github.ref_name }}" \
            --generate-notes \
            artifacts/*.tar.gz artifacts/*.zip
```

### Release Naming

```
STAR-v2.7.11c-linux-x64.tar.gz          # Compatible
STAR-v2.7.11c-windows-x64.zip           # Compatible
STAR-v2.7.11c-macos-x64.tar.gz          # Compatible
STARfast-v2.7.11c-linux-x64.tar.gz      # Optimized
STARfast-v2.7.11c-windows-x64.zip       # Optimized
STARfast-v2.7.11c-macos-x64.tar.gz      # Optimized
```

### Docker Images

Two tags per release:

```dockerfile
# Compatible (default)
docker build -t star:2.7.11c .
docker build -t star:latest .

# Fast
docker build --build-arg STAR_FAST_MODE=ON -t star:2.7.11c-fast .
docker build --build-arg STAR_FAST_MODE=ON -t star:fast .
```

Dockerfile addition:
```dockerfile
ARG STAR_FAST_MODE=OFF
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DSTAR_FAST_MODE=${STAR_FAST_MODE} \
    && cmake --build build -j$(nproc)
```

---

## CI/CD Pipeline

### PR Builds (on every push/PR)

```yaml
strategy:
  matrix:
    include:
      - name: "Linux GCC (compatible)"
        os: ubuntu-latest
        fast_mode: OFF
        test: full        # Output comparison against reference
      - name: "Linux GCC (fast)"
        os: ubuntu-latest
        fast_mode: ON
        test: smoke       # Runs OK, stats within ±1%
      - name: "Windows MSVC (compatible)"
        os: windows-latest
        fast_mode: OFF
        test: smoke
      - name: "Windows MSVC (fast)"
        os: windows-latest
        fast_mode: ON
        test: smoke
      - name: "Linux ASAN (compatible)"
        os: ubuntu-latest
        fast_mode: OFF
        asan: ON
        test: smoke
```

### Validation Criteria

| Variant | Gate | Test | Pass Criteria |
|---|---|---|---|
| Compatible | **Release gate** | Full 434M reads vs `orig_count` | 21/21 files byte-identical |
| Compatible | PR gate | Smoke 1M reads vs `smoke_ref` | Output matches reference |
| Compatible | PR gate | ASAN smoke | No memory errors |
| Fast | PR gate | Smoke 1M reads | Exit code 0, mapping stats within ±1% of compatible |
| Fast | Release info | Full 434M reads | Mapping stats logged in release notes (not gated) |

### Release Checklist

```
[ ] All CI checks pass (both variants, all platforms)
[ ] Compatible variant: full test 21/21 match
[ ] Fast variant: full test runs successfully
[ ] Version bumped in CMakeLists.txt + vcpkg.json
[ ] Tag created: git tag -a v2.7.11c
[ ] Release workflow publishes 6 binaries + 2 Docker images
[ ] Release notes document: what changed, perf delta, known output differences
```

---

## README Documentation

Add a "Variants" section:

```markdown
## Build Variants

STAR ships two variants:

| Variant | Binary | Output | Speed | Use Case |
|---------|--------|--------|-------|----------|
| **STAR** (default) | `STAR` / `STAR.exe` | Byte-identical to upstream 2.7.11b | Baseline | Reproducibility, clinical, validation |
| **STARfast** | `STARfast` / `STARfast.exe` | ~0.2% alignment tie-breaking differences | +10-30% faster | Exploratory analysis, large-scale batches |

Both variants produce identical cell counts, UMI counts, gene counts, and
filtered matrices. The differences are in how multi-mapper ties are broken
in the alignment stitching phase, affecting <0.2% of reads.

To build STARfast:
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DSTAR_FAST_MODE=ON
```

---

## Optimization Integration Roadmap

Order of implementation, showing which tier each lands in:

| Step | Optimization | Tier | Effort | Prerequisite |
|---|---|---|---|---|
| 1 | Upstream PR #791 (FastResetVector + PackedArray) | 1 (always ON) | Low | None |
| 2 | Upstream bug fixes (#2163, #535, #2676) | 1 (always ON) | Low | None |
| 3 | Union-Find for UMI graph | 1 (always ON) | Low | None |
| 4 | EmptyDrops binary search + memory | 1 (always ON) | Low | None |
| 5 | UMI hash-based 1-mismatch | 1 (always ON) | Medium | Step 3 |
| 6 | Opal → Parasail | 1 (always ON) | Medium | Validate byte-identical |
| 7 | Branch-and-bound + early rejection | **2 (`STAR_FAST_MODE`)** | Low | Re-apply reverted code inside `#ifdef` |
| 8 | Upstream PR #773 early skip | **2 (`STAR_FAST_MODE`)** | Medium | Classify after smoke test |
| 9 | SA prefetching | 1 (always ON) | Medium | Profile to validate no output change |
| 10 | DP stitching reformulation | **2 (`STAR_FAST_MODE`)** | High | Steps 7-8 validated |
| 11 | Batched SA queries | **Classify after testing** | High | Step 9 validated |

Steps 1-6 improve both variants equally. Steps 7-10 make STARfast progressively faster. Step 11 is the highest-risk/highest-reward change, saved for last.

---

## Files to Modify

| File | Change |
|---|---|
| `source/CMakeLists.txt` | Add `STAR_FAST_MODE` option, binary rename, Docker ARG |
| `source/Makefile` | Add `STAR_FAST_MODE` flag, `SFX` suffix |
| `source/stitchWindowAligns.cpp` | Guard Tier 2 optimizations with `#ifdef STAR_FAST_MODE` |
| `.github/workflows/build.yml` | Add fast-mode matrix entries |
| `.github/workflows/release.yml` | New: 6-binary release pipeline |
| `Dockerfile` | Add `ARG STAR_FAST_MODE` |
| `README.md` | Add variants section |

Total `#ifdef` lines: ~30 (all in `stitchWindowAligns.cpp`). No code duplication elsewhere.

---

## Migration Steps

- [ ] Add `STAR_FAST_MODE` option to CMakeLists.txt and Makefile
- [ ] Re-apply reverted pruning changes inside `#ifdef STAR_FAST_MODE` guards
- [ ] Add binary rename logic (`STARfast`)
- [ ] Update Dockerfile with `ARG STAR_FAST_MODE`
- [ ] Add CI matrix entries for both variants
- [ ] Create `.github/workflows/release.yml` with 6-binary matrix
- [ ] Update README with variants documentation
- [ ] Smoke test both variants (must both pass)
- [ ] Full test compatible variant (must match `orig_count` 21/21)
- [ ] Tag and publish first dual-variant release
