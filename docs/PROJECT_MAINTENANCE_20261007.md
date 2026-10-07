# C++ maintenance implementation and verification — 2026-10-07

Scope: build/dependency maintenance, bounded ownership and UB repairs, stronger
independent validation, release provenance, and preparation for C++20. C++17
remains the default. This record describes a local, uncommitted candidate based
on `9a6d932657fae942bec7e8d4936ce513d66abb5d`, not a newly published release.

## Implemented decisions

- CMake is the single source-list/flag/dependency authority. GNU Make delegates
  normal, long-read, debug, macOS and POSIX shared-memory entry points. Full-static
  legacy targets fail explicitly until a static toolchain is qualified.
- Debug/Release flags are configuration-aware; tests and production share
  sanitizer helpers. CUDA device code is not instrumented by CPU sanitizers.
- C++17/20 are explicit options and isolated presets. GCC/Clang and MSVC CI
  compare language levels; release defaults remain unchanged.
- Generic builds no longer globally require AVX2. Parasail retains runtime SIMD
  dispatch; optional AVX2 builds remain available.
- Seven numeric macros become width-checked aliases with unchanged underlying
  types. The legacy `uint` macro is deliberately retained pending a namespaced
  migration because POSIX headers already define `uint` differently.
- ClipCR4 owns storage and Parasail handles with vectors/RAII and rejects invalid
  dimensions. ReadAlign/BAMoutput forbid copying owned buffers. PackedArray
  rejects invalid widths/overflow and retains intentional borrowed/shallow views.
- Actual sanitizer findings are repaired: uninitialized transcript flags,
  references formed through null transcriptome placeholders, a soft-clipping
  stack object's escaped address, and unaligned/aliasing accesses in suffix
  comparison, byte-order helpers, SJ records and BAM buffers. Packed record sizes
  and output ordering remain unchanged; no padding-based memory expansion.
- FASTA extraction no longer uses removed/unbounded `istream >> char*`;
  multiline FASTA and oversized-header rejection are exercised end to end.
- The genome baseline is frozen to the independently released commit above;
  identical binary self-comparison is rejected. Binary/input hashes are retained.
- Dependencies use hash-verified archives and documented local compatibility
  patches. Release workflows collect actual build/dependency versions, notices
  and SHA256SUMS, and run native analysis/WebUI integration before publishing.
- QEMU Action is pinned to v4.4.0's commit. CodeQL no longer pretends a blanket
  vendored-path exclusion proves safety. Intentional HTTP support in a library
  requires call-site/reachability review, not replacing every HTTP URL by HTTPS.
- Unix scripts use LF checkout rules. Parasail's fixture path, s390x CPU guard,
  and tiny-fixture thread/time limits are declared bounded patches.

## Dependency decisions

| Dependency | Previous | Candidate | Decision |
| --- | --- | --- | --- |
| HTSlib | 1.21 | 1.24 | Official complete tarball; reapply minimal MSVC compatibility |
| bundled htscodecs | 1.6.1 | 1.6.7 | Keep the version bundled by HTSlib |
| cpp-httplib | 0.20.0 | 0.60.0 | Compile and test the actual HTTP server |
| nlohmann/json | 3.11.3 | 3.12.0 | Test JSON properties and malformed-request rejection |
| doctest | 2.4.11 | 2.5.3 | Test-only dependency |
| Parasail | 2.6.2 | 2.6.2 | Retain reviewed release; do not substitute unqualified master |
| zlib fallback | 1.3.2 | 1.3.2 | Retain; actual system version is recorded separately |
| setup-qemu-action | v3 | v4.4.0 | Pin reviewed commit; hosted-runner execution still pending |

The obsolete vendored `hfile_s3_write.c` is removed because HTSlib 1.24 removed
it upstream; its previous contents remain recoverable from Git. This is not
removal of any upstream commits. No source data or prior release was deleted.

## Executed local checks

- GCC 13.3, C++17 Release: all 93 root CTest cases pass.
- GCC 13.3, C++20 Release: all 93 root CTest cases pass.
- MSVC 19.51, C++17 and C++20 Release: all 93 root CTest cases pass.
- GCC Debug ASan+UBSan: all 93 root CTest cases pass with unit leak detection on.
- Python harness: all 13 failure-gate tests pass.
- Linux and Windows C++17 miniature analysis matches the frozen released binary:
  normalized mapped/spliced/unmapped BAM records, SJ output, coordinate and
  transcriptome BAM, multiline FASTA, Gene/GeneFull and Velocity integer matrices
  and axes. Tests also reject oversized FASTQ/FASTA headers and malformed SAM tags.
- GCC C++20 miniature analysis matches the updated C++17 candidate on the same
  fixture and sparsity setting. Native Windows C++20 matches the frozen Windows
  release on that fixture as well.
- ASan+UBSan miniature analysis passes, including FASTA, SAM B arrays, sorted
  BAM/transcriptome output and all three Velocity raw layers. Leak detection is
  disabled only for full CLI process-lifetime allocations, not unit tests.
- 8 Mb genome indices match the frozen baseline byte for byte across single
  thread, 16 threads, and 16-thread low-RAM chunking. All six index artifacts match.
- 2 Mb / 12-junction index insertion passes ASan+UBSan.
- Embedded WebUI health/properties/HTML/jobs/malformed-JSON smoke tests pass on
  Linux and Windows. This is endpoint validation, not visual UI acceptance.
- Product-only CMake installation includes binaries, build records and notices
  on Linux and Windows; Make frontend builds/copies the Linux binary.
- Workflow YAML, dependency-lock JSON and preset JSON parse; presets enumerate.

Small execution receipts/build logs are retained locally under
`data/validation/dependency-upgrade-20261007/`; miniature harness output paths
are printed in those logs and include actual commands/artifacts/binary hashes.
Ignored validation artifacts are not source code and are not automatically
published. Build records describe configuration/dependencies; full reproduction
also requires the committed source tree, platform/toolchain and execution inputs.

## Boundaries and rejected shortcuts

- Modified hosted CI has not run: Clang, both macOS architectures, s390x/QEMU v4
  and the five-platform publication gate are not certified by these local passes.
- C++20 default promotion still requires the matrix and checklist in
  [CXX20_UPGRADE_PLAN.md](CXX20_UPGRADE_PLAN.md), including CUDA shared-header
  compilation and representative runtime/RSS measurements. No speedup is claimed.
- Miniature scientific equivalence is not proof of all data/modes. Real paired
  reads, long-read, shared-memory runtime and non-AVX2-machine checks remain
  qualification work before broad compatibility claims.
- Existing full-CLI allocation cleanup and remaining public-header namespace,
  ownership/global-state/dead-code migrations are staged work, not silently
  completed. Do not add destructors to borrowed PackedArray storage or delete
  old-looking alignment modes without reachability evidence and fixtures.
- Upstream code contains further compiler warnings. Suppressing everything or
  upgrading the language switch alone would not repair them.
- No commit, push, alert dismissal, tag or new release was performed in this task.
