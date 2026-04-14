# HTSlib Upgrade Plan: 1.3 → 1.21

## Status: Complete
Last updated: 2026-04-13

## History

1. **Initially deferred** — evaluated risk/reward and decided 1.3 with Windows patches was sufficient.
2. **Later completed** — upgraded bundled HTSlib from 1.3 to 1.21 in commit `c4b944f`. Windows patches re-applied to 1.21 codebase.

## Current State

- Bundled HTSlib is **1.21** (`source/htslib/version.h`)
- Windows compatibility patches maintained on top of 1.21
- Output verified byte-identical to upstream Linux STAR
