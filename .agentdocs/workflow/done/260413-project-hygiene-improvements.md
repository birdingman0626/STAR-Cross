# Project Hygiene Improvement Plan

## Status: Plan Complete — Awaiting Execution
Last updated: 2026-04-13

## Goal

Close the gaps not covered by the current workflow plans: contributor guidance drift, build-system parity, scripted validation, vendored-code boundaries, and fork-specific maintenance rules.

---

## Priority 1: Align Tooling and Documentation (Effort: 30 min)

**Current:** The repository documents a C++17 baseline, but some tooling metadata still references older assumptions.

### Changes

- [ ] Update `source/.clang-format` to reflect the active C++17 standard
- [ ] Review `README.md`, `CONTRIBUTING.md`, and build comments for stale references to C++11-era behavior
- [ ] Confirm `clang-tidy` invocation examples still match the current `build/` layout

**Impact:** Formatting, linting, and docs describe the same language baseline and reduce contributor confusion.

---

## Priority 2: Define Make/CMake Parity Rules (Effort: 1-2 hours)

**Current:** `source/Makefile` and `source/CMakeLists.txt` are both first-class build paths, but there is no explicit policy for keeping source lists, flags, and options synchronized.

### Changes

- [ ] Add a short parity section to contributor docs describing the requirement to update both build systems together
- [ ] Audit source file lists, feature flags, and platform options for drift
- [ ] Add a lightweight CI or scripted check to catch missing source-file parity where practical

**Impact:** Reduces silent build breakage on one build path when contributors only update the other.

---

## Priority 3: Convert Validation Procedures into Scripts (Effort: 2-4 hours)

**Current:** Release validation exists as Markdown procedures in `.agentdocs/workflow/260412-release-validation-test.md`, but not as reusable scripts.

### Changes

- [ ] Add repeatable smoke-test and release-compare scripts under a stable location such as `scripts/` or `test/`
- [ ] Parameterize paths for `data/`, reference outputs, and the test binary
- [ ] Make CI and local contributors call the same scripts instead of duplicating shell snippets
- [ ] Document Windows-specific invocation differences alongside the scripts

**Impact:** Verification becomes executable, reproducible, and easier to automate.

---

## Priority 4: Reconcile Fork-Specific Contributor Docs (Effort: 1 hour)

**Current:** `CONTRIBUTING.md` still points bug reports and PR expectations at the upstream repository flow, while this fork has its own maintainer and issue path in `README.md`.

### Changes

- [ ] Update `CONTRIBUTING.md` to point contributors to this fork’s issue tracker and support flow
- [ ] Clarify which upstream docs still apply and which are fork-specific
- [ ] Add a short maintainer workflow note for output-compatibility-sensitive changes

**Impact:** Contributors stop following the wrong repository process.

---

## Priority 5: Document Vendored-Code Boundaries (Effort: 1 hour)

**Current:** `source/htslib/`, `source/opal/`, and `source/SimpleGoodTuring/` are bundled dependencies, but the repository does not clearly state how and when they may be modified.

### Changes

- [ ] Add a “vendored code” policy to contributor docs
- [ ] Require patches to bundled code to record upstream source/version and local rationale
- [ ] Define the minimum verification expected after touching bundled dependencies

**Impact:** Dependency edits become intentional and traceable instead of ad hoc.

---

## Priority 6: Improve Review Scaffolding on GitHub (Effort: 1-2 hours)

**Current:** Review expectations exist in prose, but GitHub does not guide authors or reviewers toward the expected information.

### Changes

- [ ] Add `.github/pull_request_template.md`
- [ ] Add issue templates for bug reports and enhancement requests
- [ ] Add `.github/CODEOWNERS` for major ownership boundaries (`source/`, vendored code, CI/docs)
- [ ] Require verification notes and behavior impact in the PR template

**Impact:** PRs and issues arrive with the information maintainers already need.

---

## Priority 7: Add Fork-Specific Regression Tests (Effort: 1 day)

**Current:** The main validation plan covers output compatibility, but fork-specific behavior such as Windows compatibility shims and platform limitations lacks targeted regression coverage.

### Changes

- [ ] Add focused tests for Windows-native paths (`wincompat.h`, temporary-file read pipeline, unsupported shared-memory genome load behavior)
- [ ] Add tests for build-only portability fixes such as `FixedStreamBuf.h` and non-VLA replacements
- [ ] Separate fork-specific regression tests from upstream-equivalence tests in documentation and CI output

**Impact:** Prevents platform-support regressions that may not show up in the main release comparison.

---

## Priority 8: Tighten Warning Policy for First-Party Code (Effort: 2-4 hours)

**Current:** Warning strategy is broad, but first-party and vendored code are not clearly separated in enforcement.

### Changes

- [ ] Keep third-party code warning suppression scoped to vendored paths only
- [ ] Raise warning strictness for owned code in CMake and, where feasible, in Make builds
- [ ] Add a lint or build target aimed only at first-party code

**Impact:** Improves code quality without turning bundled dependencies into maintenance noise.

---

## Suggested Execution Order

| Phase | What | Effort | Risk | Depends On |
|---|---|---|---|---|
| 1 | Align tooling/docs | 30 min | None | - |
| 2 | Contributor doc reconciliation | 1 hour | None | 1 |
| 3 | Vendored-code policy | 1 hour | None | 2 |
| 4 | Make/CMake parity rules | 1-2 hours | Low | 1 |
| 5 | GitHub review scaffolding | 1-2 hours | Low | 2 |
| 6 | Validation scripts | 2-4 hours | Low | 4 |
| 7 | Fork-specific regression tests | 1 day | Medium | 6 |
| 8 | Warning-policy tightening | 2-4 hours | Medium | 4 |

## Success Criteria

- Contributor-facing docs describe this fork accurately
- Build and validation expectations are executable instead of implied
- Bundled dependencies have explicit handling rules
- Review templates collect behavior impact and verification evidence
- Fork-specific platform behavior is protected by targeted tests
- First-party code quality checks are stricter than vendored-code checks
