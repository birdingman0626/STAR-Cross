# Dependency maintenance

`dependencies.lock.json` records reviewed versions and source archive hashes.
CMake verifies downloaded archives with `URL_HASH`; configuration emits
`STAR-build-info-<configuration>.json` with the actual compiler, flags and
system zlib/HTSlib versions. System libraries can differ from the fallback.
Use `cmake --install` to collect the binary, build record and dependency notices.
Release assets include per-platform build records and a notices archive.

## Local patches

HTSlib 1.24 is refreshed from the official complete release tarball, including
its bundled htscodecs 1.6.7. Do not substitute GitHub's incomplete source archive.
Compared with that release, the local adaptation consists of:

- Minimal zlib-only `config.h`, generated version/config-vars headers, and the
  existing force-included `win32_compat.h` / `win32_stubs/` MSVC compatibility layer.
- `hts_internal.h`: omit C's static-array parameter qualifier for MSVC;
  dispatch microsecond sleep through the compatibility function on Windows.
- Windows worker identity uses `GetCurrentThread` / `GetThreadId`; current-thread
  pseudo handles are used only for identity, never closed or joined.
- CMake owns the compiled source list and includes `simd.c` from the official list.
- BAM array lengths, CRAM cache-path capacity, VCF phasing dimensions and
  frequency-model predecessor access have locally tested boundary hardening.
  See `SECURITY_SCAN_REVIEW_20261009.md` for evidence and remaining findings.

The Windows compatibility layer predates this upgrade. It is not a replacement
for full upstream POSIX support; qualify BAM/CRAM and multithreaded compression
on native Windows whenever HTSlib changes.

Parasail 2.6.2 is still the latest official release reviewed on 2026-10-07.
`PatchParasail.cmake` fixes the fetched project's fixture path and non-x86 s390x
CPUID guard, and bounds the tiny verification fixture's OpenMP threads/time.
It also checks profile/allocation bounds and widens gap-penalty arithmetic in
the compiled kernels and generator templates. These are local patches, not an
upstream release upgrade or a claim that all library findings are resolved.
Changes happen before configuration, consistently for fresh builds. With custom
`FETCHCONTENT_SOURCE_DIR_PARASAIL`, apply the same patch explicitly before configuring.

## Update procedure

`source/cmake/htslib-patches/` records the minimal release-based compatibility
patch and the normalized content inventory. Verify it without changing the
vendored tree:

```sh
python scripts/maintain_htslib.py --archive /path/to/htslib-1.24.tar.bz2
```

The archive must match the lock SHA256. Verification reconstructs 962 maintained
files in private scratch storage and checks the current vendor against the same
inventory. Non-build files retained from older upstream checkouts are separately
fingerprinted; they are not copied into the compatibility patch. This checks
file contents, not executable permissions or runtime behavior. The Linux CI job
performs the reconstruction check before integration qualification.

For an upgrade, review the new release and port compatibility changes first,
then generate a **new** queue with `--record --queue <new-directory>`. Inspect
its patch and inventory before replacing the old queue. Recording refuses to
overwrite a queue. Patch drift fails verification; there is no fuzzy application.

1. Read the official release notes and assess the APIs actually used.
2. Verify the release archive, update its URL/hash and the lock record together.
3. Reapply only documented compatibility changes; remove upstream-fixed patches.
4. Build GCC and MSVC; run unit tests including BGZF/BAM/CRAM, sanitized tests,
   frozen-binary alignment/count comparisons and genome-index equivalence.
5. Check WebUI endpoints after HTTP/JSON changes. Check CUDA compilation separately
   when changing shared headers or the language/toolchain requirements.
6. Run all five release targets before publishing; local two-platform checks do
   not certify macOS or s390x.

Dependabot currently maintains Actions. It does not fully maintain vendored
C/C++ sources: schedule explicit review of this file/lock and official releases.
Do not ignore all third-party security findings merely because code is vendored.

## Licenses

Keep upstream per-file notices. HTSlib and htscodecs contain additional licenses
beyond their top-level notices; Parasail bundles SIMDe with its own MIT notice.
System zlib and compiler/OpenMP runtime redistribution remains subject to the
selected distribution/toolchain's licenses. doctest is only linked into tests.
