# DevOps Improvement Plan

## Status: Plan Complete — Awaiting Execution
Last updated: 2026-04-13

## Current Maturity: 4.25/10

| Component | Score | Status |
|---|---|---|
| CI/CD | 4/10 | Basic — builds on 3 platforms, no real testing |
| Docker | 6/10 | Multi-stage Dockerfile exists, missing best practices |
| Release Process | 2/10 | Manual batch scripts, no automation |
| Package Management | 3/10 | vcpkg.json + FetchContent only |
| Developer Experience | 7/10 | clang-format/tidy/editorconfig present |
| Security | 6/10 | Adequate .gitignore, no dependency scanning |
| Build Reproducibility | 6/10 | SOURCE_DATE_EPOCH in Makefile, not in CMake |
| Monitoring | 0/10 | Nothing |

---

## Priority 1: Automated Release Pipeline (Effort: 1 day)

**Current:** Manual batch scripts with hardcoded paths. No GitHub Releases. No binary distribution.

### Plan

- [ ] Create `.github/workflows/release.yml` triggered on tag push (`v*`)
- [ ] Build matrix: Linux (static), Windows (MSVC), macOS (GCC via Homebrew)
- [ ] Upload binaries as release assets via `actions/upload-release-asset`
- [ ] Auto-generate changelog from commit history since last tag
- [ ] Template:
  ```yaml
  on:
    push:
      tags: ['v*']
  jobs:
    build:
      strategy:
        matrix:
          include:
            - os: ubuntu-latest
              target: STARstatic
            - os: windows-latest
              target: STAR.exe
            - os: macos-latest
              target: STAR
      steps:
        - checkout
        - build (cmake or make per platform)
        - upload artifact
    release:
      needs: build
      steps:
        - download all artifacts
        - gh release create $TAG --generate-notes
        - attach binaries
  ```

- [ ] Add version tagging script: `git tag -a v2.7.11b -m "Release 2.7.11b" && git push --tags`

**Impact:** Users get pre-built binaries from GitHub Releases instead of compiling from source.

---

## Priority 2: CI Testing Beyond Smoke (Effort: 2 days)

**Current:** CI only runs `STAR --version`. No functional testing whatsoever.

### Plan

- [ ] Add CTest integration to CI (already have `enable_testing()` in CMakeLists.txt)
- [ ] Add sanitizer build to CI matrix:
  ```yaml
  - name: ASAN build
    run: cmake -B build -DSTAR_ASAN=ON && cmake --build build
  ```
- [ ] Add a minimal integration test using bundled test data:
  - Bundle a tiny genome (e.g., chr22 subset) + 10K reads in `test/data/`
  - Run genome generation + alignment in CI
  - Validate exit code + output file existence
- [ ] Add clang-tidy check step (already have `.clang-tidy` config)
- [ ] Add clang-format check step (diff-only, no auto-fix):
  ```yaml
  - name: Check formatting
    run: find source -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
  ```

**Impact:** Catch bugs before merge instead of during manual testing.

---

## Priority 3: Pre-commit Hooks (Effort: 2 hours)

**Current:** No hooks. Developers can push unformatted or tidy-violating code.

### Plan

- [ ] Create `.pre-commit-config.yaml`:
  ```yaml
  repos:
    - repo: https://github.com/pre-commit/pre-commit-hooks
      rev: v4.5.0
      hooks:
        - id: trailing-whitespace
        - id: end-of-file-fixer
        - id: check-merge-conflict
        - id: check-added-large-files
          args: ['--maxkb=500']
    - repo: https://github.com/pre-commit/mirrors-clang-format
      rev: v17.0.6
      hooks:
        - id: clang-format
          files: ^source/(?!htslib|opal|SimpleGoodTuring).*\.(cpp|h)$
  ```
- [ ] Add instructions to CONTRIBUTING.md: `pip install pre-commit && pre-commit install`

**Impact:** Prevents formatting drift and accidental large file commits.

---

## Priority 4: Docker Hardening (Effort: 2 hours)

**Current:** Runs as root, no .dockerignore, two competing Dockerfiles.

### Plan

- [ ] Delete `extras/docker/Dockerfile` (outdated wget-based approach)
- [ ] Fix root Dockerfile:
  ```dockerfile
  # Add non-root user
  RUN useradd -m -s /bin/bash star
  USER star
  WORKDIR /home/star
  
  # Add healthcheck
  HEALTHCHECK --interval=30s --timeout=5s \
    CMD ["STAR", "--version"] || exit 1
  ```
- [ ] Create `.dockerignore`:
  ```
  .git
  data/
  source/build*/
  *.exe
  *.pdb
  *.o
  .claude/
  .agentdocs/
  ```
- [ ] Add Docker build to CI:
  ```yaml
  - name: Build Docker image
    run: docker build -t star:${{ github.sha }} .
  - name: Test Docker image
    run: docker run star:${{ github.sha }} --version
  ```

**Impact:** Smaller image, security compliance, CI-verified container.

---

## Priority 5: Dependency Scanning (Effort: 1 hour)

**Current:** No automated dependency updates or vulnerability scanning.

### Plan

- [ ] Enable GitHub CodeQL for C++ (free for open source):
  ```yaml
  # .github/workflows/codeql.yml
  - uses: github/codeql-action/init@v3
    with:
      languages: cpp
  - uses: github/codeql-action/analyze@v3
  ```
- [ ] Add Dependabot for GitHub Actions:
  ```yaml
  # .github/dependabot.yml
  version: 2
  updates:
    - package-ecosystem: github-actions
      directory: /
      schedule:
        interval: weekly
  ```
- [ ] Document bundled dependency versions in a DEPENDENCIES.md or SBOM

**Impact:** Automated security alerts for known vulnerabilities. GitHub Actions stay current.

---

## Priority 6: Build Reproducibility (Effort: 1 hour)

**Current:** CMake embeds hostname and timestamp — non-deterministic builds.

### Plan

- [ ] Add `SOURCE_DATE_EPOCH` support to CMakeLists.txt:
  ```cmake
  if(DEFINED ENV{SOURCE_DATE_EPOCH})
      string(TIMESTAMP BUILD_DATE "%b %d %H:%M:%S" UTC)
  else()
      string(TIMESTAMP BUILD_DATE "%b %d %H:%M:%S")
  endif()
  ```
- [ ] Strip hostname from release builds (only useful during development)
- [ ] Pin compiler versions in CI via explicit `apt-get install gcc-13`

**Impact:** Reproducible binaries for verification and compliance.

---

## Priority 7: Performance Regression Detection (Effort: 1 day)

**Current:** Zero performance tracking. No way to detect regressions.

### Plan

- [ ] Create `test/benchmark.sh`:
  ```bash
  # Run alignment on fixed test data, record wall time + mapping stats
  time STAR --runMode alignReads ... --outFileNamePrefix bench/
  grep "Uniquely mapped" bench/Log.final.out
  ```
- [ ] Track metrics in CI as GitHub Actions job summaries:
  ```yaml
  - name: Benchmark
    run: |
      /usr/bin/time -v ./STAR ... 2>&1 | tee bench.log
      echo "## Benchmark Results" >> $GITHUB_STEP_SUMMARY
      grep "wall clock" bench.log >> $GITHUB_STEP_SUMMARY
  ```
- [ ] For long-term tracking: GitHub Actions cache benchmark history, compare against baseline, fail if >10% regression

**Impact:** Catches performance regressions before they reach production.

---

## Priority 8: Clean Up Technical Debt (Effort: 2 hours)

- [ ] Delete `.travis.yml` — replaced by GitHub Actions, confuses contributors
- [ ] Delete `extras/docker/Dockerfile` — superseded by root Dockerfile
- [ ] Consolidate `data/run_release.bat` / `data/run_release_test.bat` into CI workflow
- [ ] Remove hardcoded paths from batch scripts
- [ ] Add `DEVELOPMENT.md` with contributor setup guide (IDE config, build steps, test data)

---

## Lower Priority (Do Later)

### Package Distribution
- [ ] Homebrew formula for macOS (`brew install star-aligner`)
- [ ] Conda recipe for Bioconda channel
- [ ] Create a `release/` directory with platform-specific packaging scripts

### Binary Signing
- [ ] Code-sign Windows binaries (optional, requires signing certificate)
- [ ] Notarize macOS binaries (required for Gatekeeper)

### Structured Logging
- [ ] Replace ad-hoc `Log.out` writes with severity levels (INFO, WARN, ERROR)
- [ ] Machine-parseable format for pipeline integration

---

## Execution Order

| Priority | Task | Effort | Impact |
|---|---|---|---|
| 1 | Automated release pipeline | 1 day | High — users get binaries |
| 2 | CI testing (sanitizers, clang-tidy, integration) | 2 days | High — catch bugs early |
| 3 | Pre-commit hooks | 2 hours | Medium — prevent drift |
| 4 | Docker hardening | 2 hours | Medium — security compliance |
| 5 | Dependency scanning (CodeQL, Dependabot) | 1 hour | Medium — automated security |
| 6 | Build reproducibility | 1 hour | Low — compliance/verification |
| 7 | Performance regression detection | 1 day | Medium — prevent slowdowns |
| 8 | Clean up tech debt | 2 hours | Low — reduce confusion |
