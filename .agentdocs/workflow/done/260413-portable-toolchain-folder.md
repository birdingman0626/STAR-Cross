# Portable Toolchain Folder Plan

## Status: Pending
Last updated: 2026-04-13

## Goal

Use a single repo-local `toolchain/` folder to cache build tools and compilers for Windows development and GitHub Actions release builds, without requiring global installation of CMake, Ninja, or LLVM.

## Scope

This plan covers:

- local Windows builds
- GitHub Actions online-only builds
- release automation using the same cached folder layout
- keeping `toolchain/` out of git

## Repository decision

Use one top-level cache root:

```text
toolchain/
  cmake/
  ninja/
  llvm/
  llvm-mingw/        # optional future lane
  icx/               # optional metadata or launcher hooks only
  cache/
```

`toolchain/` is cache/runtime state and should never be versioned.

---

## Phase 1: Standardize Folder Layout

**Value:** High  
**Risk:** Low

### Changes

- [ ] Adopt `toolchain/` as the only repo-local tool cache root
- [ ] Put portable CMake under `toolchain/cmake/`
- [ ] Put portable Ninja under `toolchain/ninja/`
- [ ] Put portable LLVM/Clang under `toolchain/llvm/`
- [ ] Reserve optional future folders for:
  - `toolchain/llvm-mingw/`
  - `toolchain/cache/`

### Verification

- [ ] Tool paths are deterministic and documented
- [ ] No build instructions require a machine-global CMake install

---

## Phase 2: Add Bootstrap Scripts

**Value:** High  
**Risk:** Low

### Goal

Make one command populate `toolchain/` on a clean Windows machine.

### Changes

- [ ] Add a bootstrap script, e.g. `scripts/bootstrap_toolchain.ps1`
- [ ] Download and unpack:
  - CMake Windows ZIP
  - Ninja Windows ZIP
  - LLVM Windows package or ZIP
- [ ] Verify expected executables exist:
  - `toolchain/cmake/bin/cmake.exe`
  - `toolchain/ninja/ninja.exe`
  - `toolchain/llvm/bin/clang-cl.exe`
- [ ] Print environment hints for local use

### Verification

- [ ] Bootstrap succeeds on a clean Windows machine
- [ ] Re-running the script is idempotent

---

## Phase 3: Use `toolchain/` for Local Compilation

**Value:** High  
**Risk:** Low

### Goal

Compile entirely through the cached folder layout.

### Changes

- [ ] Add documented build commands using:
  - `toolchain/cmake/bin/cmake.exe`
  - `toolchain/ninja/ninja.exe`
  - `toolchain/llvm/bin/clang-cl.exe`
- [ ] Add a local build helper script, e.g. `scripts/build_clangcl.ps1`
- [ ] Standardize output directory names such as:
  - `source/build-clangcl`
  - `source/build-msvc`

### Example command shape

```powershell
toolchain\cmake\bin\cmake.exe -S source -B source\build-clangcl -G Ninja `
  -DCMAKE_MAKE_PROGRAM=toolchain\ninja\ninja.exe `
  -DCMAKE_C_COMPILER=toolchain\llvm\bin\clang-cl.exe `
  -DCMAKE_CXX_COMPILER=toolchain\llvm\bin\clang-cl.exe
toolchain\cmake\bin\cmake.exe --build source\build-clangcl
```

### Verification

- [ ] Local build succeeds without system CMake
- [ ] Smoke test can run against the produced binary

---

## Phase 4: Add CMake Presets for Portable Tools

**Value:** High  
**Risk:** Low

### Goal

Reduce command duplication and make CI/local invocation consistent.

### Changes

- [ ] Add `CMakePresets.json`
- [ ] Add presets for:
  - `windows-clangcl-release`
  - `windows-msvc-release`
  - `windows-asan` if supported
- [ ] Point presets at `toolchain/` executables where practical
- [ ] Keep presets repo-relative

### Verification

- [ ] Presets work locally after bootstrap
- [ ] Presets can be reused in GitHub Actions

---

## Phase 5: Integrate `toolchain/` into GitHub Actions

**Value:** High  
**Risk:** Low-Medium

### Goal

Use GitHub Actions to build and publish Windows binaries with no dependency on local developer machines.

### Changes

- [ ] Add workflow steps that create/populate `toolchain/`
- [ ] Cache downloaded archives and unpacked folders using `actions/cache`
- [ ] Build with repo-local CMake/Ninja/LLVM paths
- [ ] Run smoke test after build
- [ ] Upload artifacts

### Suggested cache keys

- CMake version
- Ninja version
- LLVM version
- workflow OS

### Verification

- [ ] Repeated workflow runs reuse cached `toolchain/` contents
- [ ] Windows artifacts can be produced entirely online

---

## Phase 6: Add Tag-Driven Release Automation

**Value:** High  
**Risk:** Medium

### Goal

Publish release binaries from GitHub Actions using the portable toolchain cache.

### Changes

- [ ] Add a release workflow triggered by tags like `v*`
- [ ] Build release binaries using the `toolchain/` folder
- [ ] Run smoke validation before publishing
- [ ] Upload release assets to GitHub Releases
- [ ] Optionally publish:
  - primary MSVC build
  - secondary `clang-cl` build

### Verification

- [ ] Tag push produces downloadable release assets
- [ ] Release binaries are built reproducibly from CI only

---

## Phase 7: Document Update / Invalidation Rules

**Value:** Medium  
**Risk:** Low

### Goal

Prevent stale toolchains from causing hard-to-debug build differences.

### Changes

- [ ] Document when to refresh `toolchain/`:
  - LLVM upgrade
  - CMake upgrade
  - Ninja upgrade
  - cache corruption
- [ ] Add a cleanup script or `-ForceRefresh` bootstrap mode
- [ ] Record tool versions in workflow logs

### Verification

- [ ] Developers can intentionally refresh the cache
- [ ] CI logs show exactly which tool versions were used

---

## Out of scope

- [ ] Checking compiler binaries into git
- [ ] Replacing MSVC or ICX entirely
- [ ] Adding multiple experimental compiler lanes before the basic LLVM lane is stable

---

## Recommended implementation order

| Phase | Work | Effort | Risk | Depends On |
|---|---|---|---|---|
| 1 | `toolchain/` layout + docs | 30 min | Low | - |
| 2 | Bootstrap script | 1-2 hours | Low | 1 |
| 3 | Local build helper | 1 hour | Low | 2 |
| 4 | CMake presets | 1-2 hours | Low | 2 |
| 5 | GitHub Actions cache/build integration | 2-4 hours | Low-Medium | 4 |
| 6 | Tag-driven release workflow | 2-4 hours | Medium | 5 |
| 7 | Refresh/cleanup policy | 30-60 min | Low | 2 |

## Success criteria

- Windows builds work with no machine-global CMake install
- `toolchain/` is the single source of truth for portable build tools
- GitHub Actions can build and publish release binaries entirely online
- Cached tool versions are explicit, reproducible, and easy to refresh
