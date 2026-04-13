## Summary
<!-- Brief description of what this PR does -->

## Output Compatibility
- [ ] Smoke test passes (1M reads, count matches reference)
- [ ] No change to integer count matrices
- [ ] EM probability files: no change / acceptable FP rounding only

## Checklist
- [ ] Both CMake and Makefile build paths updated (if source files added/removed)
- [ ] No modifications to vendored code (htslib/, opal/, SimpleGoodTuring/) without documented rationale
- [ ] New code uses C++17 features where appropriate
- [ ] No new compiler warnings on MSVC `/W4` or GCC `-Wall -Wextra`
