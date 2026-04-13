# HTSlib Upgrade Plan: 1.3 → 1.21

## Background

The bundled HTSlib in `source/htslib/` is version **1.3** (released 2016). The current stable release is **1.21** (Dec 2024). This is nearly a decade of missed bug fixes, performance improvements, security patches, and new format support.

This fork carries **custom Windows compatibility patches** on top of the bundled HTSlib, so the upgrade requires re-applying those patches to the newer codebase.

## Why Upgrade

- **Security**: Years of memory safety fixes and CVE patches.
- **Performance**: Improved BAM/CRAM I/O, better multithreaded support.
- **Format support**: CRAM 3.1, additional codec support.
- **Correctness**: Many edge-case bug fixes in BAM parsing/writing.
- **Maintainability**: Closer to upstream makes future upgrades easier.

## Current Windows Patches Inventory

Before upgrading, catalog every custom modification in `source/htslib/`:

| File | Modification |
|---|---|
| `version.h` | Auto-generated, defines `HTS_VERSION "1.3"` |
| `win32_compat.h` | Windows compatibility shims (new file) |
| `win32_stubs/getopt.h` | Stub for missing POSIX header (new file) |
| `win32_stubs/sys/select.h` | Stub (new file) |
| `win32_stubs/sys/socket.h` | Stub (new file) |
| `win32_stubs/sys/statvfs.h` | Stub (new file) |
| `win32_stubs/sys/time.h` | Stub (new file) |
| `win32_stubs/unistd.h` | Stub (new file) |
| `bgzf.c` | Windows-specific modifications |
| `hfile.c` | Windows-specific modifications |
| `cram/thread_pool.h` | Windows-specific modifications |
| `htslib/hts_defs.h` | Windows-specific modifications |

### Phase 0: Detailed Patch Extraction

- [ ] Run `git diff HEAD -- source/htslib/` to get exact diffs for every modified file
- [ ] Document purpose of each change (POSIX → Win32 adaptation, missing headers, etc.)
- [ ] Identify which patches are still needed (HTSlib 1.21 has some native Windows support)

## Upgrade Phases

### Phase 1: Evaluate HTSlib 1.21 Native Windows Support

HTSlib gained incremental Windows/MSVC support over time. Before re-applying all patches:

- [ ] Check HTSlib 1.21 changelog and `INSTALL` for Windows build instructions
- [ ] Check if `htslib/hts_defs.h` now handles MSVC natively
- [ ] Check if `bgzf.c` and `hfile.c` have Windows codepaths
- [ ] Check if POSIX stubs (`unistd.h`, `sys/time.h`, etc.) are still needed
- [ ] Determine which custom patches are obsoleted by upstream changes

**Decision point**: If HTSlib 1.21 builds on MSVC with minimal changes, many patches may be droppable.

### Phase 2: Replace Bundled Source

- [ ] Download HTSlib 1.21 release tarball
- [ ] Replace `source/htslib/` contents with HTSlib 1.21 source
- [ ] Keep only the files STAR actually compiles (check Makefile and CMakeLists.txt for the file list)
- [ ] Update `version.h` to reflect 1.21

### Phase 3: Re-apply Required Patches

- [ ] Re-apply Windows compatibility patches that are still needed (from Phase 0 inventory)
- [ ] Adapt patches to new HTSlib API if signatures/structures changed
- [ ] Add new `win32_stubs/` headers if HTSlib 1.21 pulls in additional POSIX headers

### Phase 4: Build Integration

- [ ] Update `CMakeLists.txt` — HTSlib source file list may have changed (new/removed files)
- [ ] Update `Makefile` — same for traditional build
- [ ] Verify zlib linkage still works (HTSlib 1.21 may have different zlib expectations)
- [ ] Check if HTSlib 1.21 requires additional dependencies (e.g., liblzma, libbz2, libcurl) and either bundle or disable them

### Phase 5: Validation

- [ ] Build succeeds on MSVC (Windows)
- [ ] Build succeeds on GCC (Linux) via Makefile
- [ ] STAR runs genome generation on test data without error
- [ ] STAR runs alignment on test data, output matches pre-upgrade results
- [ ] BAM output is valid (samtools quickcheck or equivalent)
- [ ] No new compiler warnings

## Risks

- **API breakage**: HTSlib internal APIs may have changed between 1.3 and 1.21; STAR uses some internals directly.
- **New dependencies**: HTSlib 1.21 optionally depends on liblzma, libbz2, libdeflate, libcurl. These need to be disabled or bundled.
- **Build system complexity**: The Makefile builds HTSlib as a sub-make; CMakeLists.txt compiles individual .c files. Both need updating.
- **Regression risk**: Without a formal test suite, validation relies on manual test runs.

## Other Dependencies (No Action Needed)

| Dependency | Version | Status |
|---|---|---|
| zlib | 1.3.1 | Current |
| SIMDe | commit e8b7a2ec | Functional, low priority |
| Opal | Unversioned (2014) | Custom fork, stable |
| SimpleGoodTuring | April 2004 | Stable math code, no upstream |
| OpenMP | System | N/A |
