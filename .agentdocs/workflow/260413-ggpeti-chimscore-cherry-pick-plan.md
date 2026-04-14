# Cherry-Pick Plan: ggPeti `feat/chimScoreUsePostStitch`

## Status: Phase 1 Complete
Last updated: 2026-04-13

## Source Branch

- Repository: `ggPeti/STAR`
- Branch: `feat/chimScoreUsePostStitch`
- Compare view: `b1edc12...feat/chimScoreUsePostStitch`

## Goal

Extract the useful chimeric-alignment fixes from the branch without blindly importing experimental feature flags or branch-local packaging work.

This is not a literal commit-by-commit cherry-pick plan. The branch mixes:

- worthwhile bug fixes
- optional behavior changes
- test fixtures
- unrelated Nix packaging
- some rough/debug code

## Recommended Scope

### Pick first: chimeric coordinate and overlap fixes

These are the best candidates because the current fork still uses older logic in:

- `source/ChimericDetection_chimericDetectionMult.cpp`
- `source/ChimericAlign.cpp`
- `source/ChimericAlign_chimericStitching.cpp`
- `source/ReadAlign_chimericDetectionOld.cpp`
- `source/ReadAlign_multMapSelect.cpp`
- `source/ReadAlign_stitchWindowSeeds.cpp`
- `source/stitchWindowAligns.cpp`

### Pick second: acceptance-test concept

Adapt the `extras/tests/chim_poststitch/` fixture into this repo’s own test layout rather than copying it verbatim.

### Defer: post-stitch score gating feature

`chimScoreUsePostStitch` and `chimScorePreStitchAllowance` should be treated as a separate opt-in feature, not part of the bugfix cherry-pick.

### Skip: unrelated branch content

- Nix files: `flake.nix`, `flake.lock`, `nix/package.nix`
- debug includes / dead code
- `chimAllowSubsetTranscripts`

## Phase 1: Port Bugfixes Only

### 1.1 Block-based chimeric overlap

Replace the simple scalar overlap in `ChimericDetection_chimericDetectionMult.cpp` with the branch’s exon-block overlap calculation in read-order space.

Reason:
- current code assumes a single interval overlap
- branch logic handles multi-exon / multi-mate layouts more accurately

### 1.2 Better exon-pair selection for chimeric junctions

Port the `ChimericAlign.cpp` constructor logic that identifies the exon pair relevant to the junction instead of using the old default:

- current: `ex2 = al2->Str==0 ? 0 : al2->nExons-1`
- target: choose overlapping cross-reference exon pair, then fall back only if needed

### 1.3 Trim stitched transcripts to the junction-relevant side

Port the `ChimericAlign_chimericStitching.cpp` logic that removes exons on the wrong side of the junction before rescoring.

Reason:
- avoids rescoring transcript pieces that belong to the wrong mate / wrong reference side

### 1.4 Cross-mate `roStart` corrections

The upstream branch leaves `ReadAlign_chimericDetectionOld.cpp`, `ReadAlign_multMapSelect.cpp`,
`ReadAlign_stitchWindowSeeds.cpp`, and `stitchWindowAligns.cpp` **unchanged** relative to upstream
STAR. The only actual roStart bug was in `ChimericAlign_chimericStitching.cpp` line 36:

- Bug: `a1.Lread` used when computing `roStart1` for `a2` on the negative strand
- Fix: changed to `a2.Lread` (ported in Phase 1)

`ReadAlign_peOverlapMergeMap.cpp` was audited and does not share the same pattern.

## Phase 2: Add Regression Tests

- [ ] Create a small chimeric fixture derived from the branch test data
- [ ] Add a reproducible script or `ctest` entry that checks:
  - chimeric junction presence/absence
  - score stability
  - no regression in legacy paths
- [ ] Keep the fixture separate from release-validation data

## Phase 3: Optional Feature Branch

Only after Phase 1 is validated:

- [ ] consider `chimScoreUsePostStitch`
- [ ] consider `chimScorePreStitchAllowance`
- [ ] document semantics clearly
- [ ] verify that threshold-ratcheting behavior is mathematically and biologically sound

Do not port this feature as-is from the branch because the final branch state still contains inconsistency between comments and implementation.

## Explicit Non-Goals

- Do not cherry-pick the branch wholesale
- Do not import `chimAllowSubsetTranscripts`
- Do not copy Nix packaging into this effort
- Do not merge debug includes like `<iostream>`

## Verification

- [x] build on Windows (MSVC, Release) — clean
- [x] 45/45 unit tests pass (fixed parasail test_verify path bug in CMakeLists.txt)
- [x] smoke test: 21/21 Solo.out files byte-identical vs. smoke_ref (1M reads, CellRanger4)
- [ ] build on Linux
- [ ] run targeted chimeric regression fixture
- [ ] compare chimeric junction output before/after on a chimera-rich dataset
- [ ] confirm no change to unrelated STARsolo outputs (done implicitly by smoke test)

## Execution Order

1. [x] port overlap + exon-selection + trimming bug fixes
2. [x] port `roStart` corrections
3. add targeted regression tests
4. run smoke and representative chimeric validation
5. evaluate post-stitch score gating separately

## Bottom Line

The branch contains useful chimeric bugfix material, but it should be mined selectively.

The highest-value cherry-picks are:

- block-based chimeric overlap
- smarter chimeric exon selection
- junction-side transcript trimming before rescoring
- cross-mate `roStart` corrections

The feature flags and packaging extras should stay out of the initial port.
