# CodeQL triage and boundary hardening

Reviewed the authenticated `main` alert list on 2026-10-09, against scanned
commit `7140819`: 94 open findings (70 Parasail, 22 HTSlib, one cpp-httplib,
one STAR-Cross). This is a finding count, not a count of exploitable defects.
No alerts were dismissed. Existing unrelated architecture changes were retained.

## Implemented

| Surface / alert | Change | Verification |
| --- | --- | --- |
| BAM auxiliary arrays, #70 | Validate count against available bytes by division before multiplying. | Public BAM API rejects truncated arrays with counts `0x40000000`, `0x80000000`, `0xffffffff`, for 16/32-bit integer and float elements; valid arrays retain their round trips. |
| CRAM cache path, #90 | Account for both bytes written by unknown escapes, reject a terminal `%`, and use checked-length copies. | Compile the actual private function with exact-fit, overflow, numeric/non-numeric escape, and incomplete-escape fixtures. The old source triggers an ASan stack-buffer-overflow; the fixed source passes. |
| VCF phasing, #105 | Validate dimensions against available bytes without a narrow product; reject zero/negative ploidy. | Normal diploid phasing remains correct. The old source exhibits signed overflow and an ASan out-of-bounds access for large dimensions; the fixed source rejects them. |
| VCF FORMAT bookkeeping, #76 | Promote before the product, consistently with the existing allocation and 2 GiB bound. | Rebuilt HTSlib and format regressions. No claim of exhaustive VCF parser fuzzing. |
| HTScodecs, #91–93 | Do not compare a predecessor when at the first array element. Preserve the sentinel layout and model ordering. | 20,000-symbol first-symbol/normalization round trip plus BAM/CRAM format round trips. |
| Parasail profile/allocation arithmetic | Promote profile and matrix products; reject overflowing SIMD profile dimensions and int-indexed table dimensions; check allocation byte products. | Reject huge dimensions before touching a one-byte query; reject overflowing typed allocations. |
| Parasail widened gap arithmetic | Perform gap-penalty products in `int64_t`, including generated kernel templates. | 64-bit scan with `INT_MAX` gap penalties returns the correct perfect-match score; existing 1,000 scalar clipping score/endpoint comparisons. |
| ClipCR4, #104 | Explicit `size_t` product for the fixed 64-by-91 database. | Existing clipping tests; this was not an exploitable overflow with current constants. |
| Pileup formatting, #77/#78 | Check `snprintf` errors/truncation before advancing the write position; use defined unsigned ChEBI magnitude arithmetic. | Public pileup API preserves 32 simultaneous maximum-width ChEBI codes and probabilities exactly. This is defensive hardening, not a demonstrated exploit. |
| Error model, #66/#67 | Reject negative depth, invalid pointers and dimensions outside the sixteen internal base bins before touching the output; promote allocation products. | Invalid dimensions leave a one-float sentinel unchanged; empty sixteen-bin input preserves the zero-output behavior. |
| VCF FORMAT, #73/#74 | Reject zero-sample, negative/null and non-divisible value layouts; serialize the validated total count rather than a redundant product. | Public API rejects zero samples and three values for two samples, accepts the valid two-value case. |
| CRAM M5 cache lookup, #89 | Require exactly 32 ASCII hex digits before using the digest as a cache/search-path filename. | Private-helper fixtures reject traversal, separators, non-hex and incomplete names; valid mixed-case digests pass. Explicit local UR references retain their existing separate policy. |

HTSlib changes are recorded in the release-based patch queue and its content
manifest. Parasail changes are applied by `PatchParasail.cmake` before building;
both generated kernels and templates are covered. A fresh hash-verified Parasail
archive's allocation/SIMD/template/CMake representative files were checked
against the cached patched tree; repeated patching of these files is idempotent.
No dependency version or C++ language-version bump is involved.

## Not represented as fixed

- #103/#102: HTSlib's tagged CRAM index representation and casts. No demonstrated
  exploit; changing its ABI/layout is not justified by the warning alone.
- #80–83: `sscanf` field counts already checked before using the parsed values.
- #84: the reported array access is inside an explicitly bounded loop.
- #98/#96: protocol/path parsing, not an authentication decision.
- #106: the HTTP client proxy branch is not called by the STAR WebUI server.
  Replacing `http://` there with HTTPS would change library protocol semantics.
- #101/#100/#99/#88: third-party test utilities, not the shipped STAR executable.
- Remaining Parasail SSW/matrix paths: assess
  actual reachability and operand bounds individually. Do not blanket-suppress
  dependency code or claim all 94 findings are resolved.

## Reproduce

Local verification after the second batch: native Windows CTest **122/122**;
Linux ASan/UBSan CTest **125/125**, with leak detection and halt-on-error.
Python discovery: 49 tests, 45 passed and 4 explicit conditional skips.
Both native and sanitized
scientific integration fixtures pass. The private-helper test also passes
without sanitizers through the ordinary Linux CTest registration. The HTSlib
patch queue reconstructs all **962** maintained files and matches the current
vendor content. This does not certify the other release platforms.

```sh
cmake --build <configured-build> --parallel 4
ctest --test-dir <configured-build> --output-on-failure
python scripts/test_cpu_upstream.py --star-exe <built-STAR>
python scripts/maintain_htslib.py --archive <locked-htslib-1.24.tar.bz2>
python scripts/test_security_boundaries.py --sanitize
```

On Linux, `hts_private_security_boundaries` is also registered in CTest and uses
sanitizers when the build enables ASan. Expected-failure negative controls:

```sh
python scripts/test_security_boundaries.py --sanitize --baseline 7140819 --case cache
python scripts/test_security_boundaries.py --sanitize --baseline 7140819 --case phasing
```

Local receipts are retained under
`data/validation/architecture-20261009/security-*` (ignored, not release assets).
Normal-input integration qualifies scientific fixtures, not every possible
input and not a full real-data performance benchmark. GitHub alert resolution
requires a later push and a new CodeQL run; local tests do not establish that.

Release preflight also reproduced the earlier MSVC C++20 benchmark build
failure: nested lambdas used `threads` only in an OpenMP pragma. Explicit
value captures fix MSVC's capture analysis without changing the thread count.
The native C++20 build, all 122 CTest cases and the 1,000-record rank-hint
sweep pass. The old local build initially retained unpatched Parasail
FetchContent sources; reapplying the maintained patch before rebuilding
restored the allocation-boundary tests. C++17 remains the release default.
