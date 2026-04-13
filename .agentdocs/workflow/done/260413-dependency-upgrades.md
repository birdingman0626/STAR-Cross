# Dependency Upgrade Plan

## Status: Pending
Last updated: 2026-04-13

## Current State

| Dependency | Current | Latest | Status | Location |
|---|---|---|---|---|
| HTSlib | 1.21 | 1.21 | **Up to date** | `source/htslib/` (bundled) |
| zlib | 1.3.1 | 1.3.2 | **1 patch behind** | CMake FetchContent |
| Opal | 2014 fork | Dead upstream (2015) | **Replace with Parasail** | `source/opal/` (bundled) |
| SIMDe | commit `e8b7a2ec` | v0.8.2 | **Removed with Opal** | `source/opal/simde_avx2.h` |
| SimpleGoodTuring | April 2004 | Same | **No action** | `source/SimpleGoodTuring/` |

---

## Phase 1: zlib 1.3.1 → 1.3.2 (Effort: 5 min, Risk: None)

Patch release with security hardening and minor bug fixes.

### Changes

- [ ] Edit `source/CMakeLists.txt`: change `GIT_TAG v1.3.1` to `GIT_TAG v1.3.2`
- [ ] Edit `source/vcpkg.json`: no change needed (no version constraint on zlib)
- [ ] Clean and rebuild to pull new version
- [ ] Smoke test — must match existing output

### Verification

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Run smoke test, compare against smoke_nopruning reference
```

---

## Phase 2: Replace Opal with Parasail (Effort: 1-2 days, Risk: Low-Medium)

### Why Parasail

| Feature | Opal (current) | Parasail (replacement) |
|---|---|---|
| Last active | 2015 | Aug 2025 |
| MSVC/Windows | Custom patches (SIMDe, `_alloca`, `_aligned_malloc`) | Native CMake + MSVC support |
| SIMD | AVX2 only (via SIMDe fallback) | SSE2/SSE4.1/AVX2/AVX-512/NEON |
| ARM support | Via SIMDe emulation | Native NEON |
| Build system | Raw .cpp/.h files | CMake with FetchContent support |
| License | MIT | BSD (Battelle) |
| Peer-reviewed | No | Yes (BMC Bioinformatics 2016) |

### STAR's Opal Usage (Narrow Scope)

Opal is only used for CellRanger4-style TSO adapter clipping:

| File | Usage |
|---|---|
| `source/ClipCR4.cpp` | `opalFillOneSeq()`, `opalAlign()` — wrapper class |
| `source/ClipCR4.h` | Class declaration with `OpalSearchResult` members |
| `source/ClipMate_clipChunk.cpp` | Calls `cr4->opalAlign()` / `cr4->opalRes[idb].score` |

Total touchpoints: ~3 files, ~80 lines of glue code. The rest of STAR never calls Opal.

### Step 2.1: Add Parasail via FetchContent

```cmake
# In CMakeLists.txt, after zlib FetchContent block:
FetchContent_Declare(parasail
    GIT_REPOSITORY https://github.com/jeffdaily/parasail.git
    GIT_TAG v2.6.2
    GIT_SHALLOW TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(parasail)
```

- [ ] Add FetchContent block to `source/CMakeLists.txt`
- [ ] Link against parasail: `target_link_libraries(STAR PRIVATE parasail_static)`
- [ ] Verify parasail builds on MSVC, GCC, and Clang

### Step 2.2: Rewrite ClipCR4 Wrapper

The `ClipCR4` class needs its Opal calls replaced with Parasail equivalents.

**API Mapping:**

```
Opal API                              → Parasail API
──────────────────────────────────────────────────────────────
#include "opal.h"                     → #include <parasail.h>

opalInitSearchResult(&result)         → (no init needed, parasail_result_t* returned)

int scoreMatrix[alpha*alpha]          → parasail_matrix_t *matrix = 
                                          parasail_matrix_create("ACGT", match, mismatch)

opalSearchDatabase(                   → For each db sequence:
    query, queryLen,                      parasail_result_t *r = parasail_sw_trace_striped_16(
    db, dbLen, dbSeqLens,                     query, queryLen,
    gapOpen, gapExt,                          dbSeq, dbSeqLen,
    scoreMatrix, alphaLen,                    gapOpen, gapExt, matrix);
    results, SEARCH_SCORE_END,
    OPAL_MODE_SW, OVERFLOW_SIMPLE)

result.score                          → parasail_result_get_score(r)
result.endLocationTarget              → parasail_result_get_end_ref(r)
result.endLocationQuery               → parasail_result_get_end_query(r)

(free manually)                       → parasail_result_free(r)
                                        parasail_matrix_free(matrix)
```

**Key difference:** Opal's `opalSearchDatabase()` aligns one query against multiple database sequences in a single vectorized call (inter-sequence parallelism). Parasail aligns one pair at a time but uses intra-sequence SIMD. For STAR's use case (short adapter vs short read fragments), per-pair calling is fine — the sequences are too short for inter-sequence batching to matter.

- [ ] Rewrite `ClipCR4.h`: replace `OpalSearchResult` with parasail types
- [ ] Rewrite `ClipCR4.cpp`:
  - `opalFillOneSeq()` → store sequences in buffer (same as now)
  - `opalAlign()` → loop calling `parasail_sw_trace_striped_16()` per sequence
  - Extract score + end position from `parasail_result_t`
- [ ] Update `ClipMate_clipChunk.cpp` if interface changes (likely minimal)

### Step 2.3: Remove Opal

- [ ] Delete `source/opal/` directory entirely (opal.cpp, opal.h, simde_avx2.h, LICENSE)
- [ ] Remove opal source files from `source/CMakeLists.txt` compile list
- [ ] Remove opal source files from `source/Makefile` OBJECTS/SOURCES lists
- [ ] Remove any `#include "opal/opal.h"` references

### Step 2.4: Update Makefile (Traditional Build)

The Makefile needs parasail too. Options:

- **Option A (recommended):** Require parasail as system library for Makefile builds: `LDFLAGS += -lparasail`
- **Option B:** Vendor parasail source files directly (like opal was) — defeats the purpose
- **Option C:** Drop Makefile support for adapter clipping, make it CMake-only

- [ ] Choose approach and implement
- [ ] Test Makefile build on Linux

### Step 2.5: Validation

- [ ] Build succeeds on MSVC (Windows)
- [ ] Build succeeds on GCC (Linux)
- [ ] Build succeeds on Clang (macOS)
- [ ] Smoke test (1M reads with `--clipAdapterType CellRanger4`): output byte-identical to pre-migration
- [ ] Full release test (434M reads): all 21 Solo.out files match `orig_count`
- [ ] No new compiler warnings

---

## Phase 3: SIMDe Removal (Automatic with Phase 2)

SIMDe (`source/opal/simde_avx2.h`, 1.3MB auto-generated header) is only included by Opal. When Opal is replaced by Parasail, SIMDe is deleted automatically as part of Phase 2 Step 2.3.

Parasail handles its own SIMD abstraction internally — no external SIMDe dependency needed.

---

## Phase 4: SimpleGoodTuring Cleanup (Effort: 15 min, Risk: None)

No alternative library exists — SGT is a fixed mathematical formula (~250 lines), not an evolving library. The algorithm (Sampson & Gale, 2001) is correct and will never change. Only cosmetic cleanup needed.

**Used in:** `SoloFeature_emptyDrops_CR.cpp:77-85` — smooths ambient gene expression profile for EmptyDrops cell filtering. Runs once per dataset, takes microseconds.

### Changes

- [ ] Fix MSVC 6.0 workaround in `source/SimpleGoodTuring/sgt.h` line 86-90:
  ```cpp
  // Remove:
  #ifdef _WIN32
  #define MinInput (5)
  #else
      static const unsigned int MinInput = 5;
  #endif

  // Replace with:
      static constexpr unsigned int MinInput = 5;
  ```
  The `#define` was a workaround for MSVC 6.0 (year 2000) which didn't support `static const` in class scope. Modern MSVC handles `constexpr` fine since C++11.

- [ ] Remove `using namespace std;` from header (line 47) — pollutes global namespace for every includer. Replace with explicit `std::map`, `std::vector`, `std::less`, `std::pair`, `std::make_pair`, `std::log`, `std::fabs`, `std::sqrt`, `std::exp` qualifications.

- [ ] Verify build and smoke test pass after changes

### Not Changing

- The algorithm itself — it's mathematically correct
- The class template interface (`add()`, `analyse()`, `estimate()`)
- The file location (`source/SimpleGoodTuring/`)

---

## Not Upgrading

### HTSlib (already at 1.21)
- Upgraded from 1.3 to 1.21 in commit `c4b944f`
- Windows patches re-applied
- **Decision:** Up to date, monitor for future releases

---

## Execution Order

| Phase | What | Effort | Risk | Depends On |
|---|---|---|---|---|
| 1 | zlib 1.3.1 → 1.3.2 | 5 min | None | — |
| 2.1 | Add Parasail FetchContent | 1 hour | Low | Phase 1 (clean build) |
| 2.2 | Rewrite ClipCR4 wrapper | 4 hours | Medium | Phase 2.1 |
| 2.3 | Remove Opal + SIMDe | 15 min | None | Phase 2.2 |
| 2.4 | Update Makefile | 1 hour | Low | Phase 2.3 |
| 2.5 | Validation (smoke + full) | 2.5 hours | — | All above |
| 3 | SIMDe removal | 0 min | None | Automatic with 2.3 |
| 4 | SimpleGoodTuring cleanup | 15 min | None | — (independent) |

**Total estimated effort:** 1-2 days including full validation run.

## Post-Migration Benefits

- **3 fewer vendored files** (opal.cpp, opal.h, simde_avx2.h totaling ~25K lines)
- **Cleaner MSVC build** — no more `_alloca`, `_aligned_malloc`, SIMDe `isnan` workarounds
- **AVX-512 ready** — Parasail auto-dispatches to best available SIMD
- **ARM ready** — native NEON support for Apple Silicon / Graviton
- **Active maintenance** — security fixes and improvements flow from upstream
- **No legacy preprocessor hacks** — SGT `#define MinInput` removed
