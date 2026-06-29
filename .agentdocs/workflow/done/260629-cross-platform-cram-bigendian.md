# Cross-platform: CRAM, macOS readFilesCommand, big-endian (done 2026-06-29)

Merged via PR #7 (squash `044da13`). CI green on Linux gcc/clang, macOS aarch64, Windows MSVC, CodeQL.

## What was implemented
1. **Referenceless CRAM** (`--outSAMtype CRAM Unsorted|SortedByCoordinate`)
   - `source/cramOutput.{h,cpp}`: transcode finished BAM → CRAM with `CRAM_OPT_NO_REF` (no reference FASTA). Called from `STAR.cpp` after wiggle output; removes intermediate BAM; on failure keeps BAM and warns (non-fatal).
   - `Parameters_runtimeSetup.cpp`: `outSAMtype[0]=="CRAM"` reuses the BAM path, sets `outCRAMbool` + `outCRAMfile{Unsorted,Coord}Name`. CRAM incompatible with `--outStd`.
   - Hot per-record output path untouched. Transcriptome BAM stays BAM.
   - WebUI: dropdown options, `.cram` artifacts + MIME.
   - Deliberately NOT a port of upstream draft #2670 (broken: NULL header, rerouted working BAM). Referenceless chosen over reference-based (quality scores dominate file size; ref only shrinks SEQ → ~10-20% gain for much more complexity).
2. **macOS readFilesCommand** (#2693/#2663): `Parameters_openReadsFiles.cpp` POSIX branch uses `posix_spawnp` "<shell> <script>" instead of `vfork()`+`execlp()`+`exit()`. Windows `system()` path unchanged.
3. **Big-endian** (#2690): `source/byteOrder.h` (`loadUintLE`/`storeUintLE`, `STAR_BIG_ENDIAN` guard). Routed `PackedArray::operator[]`/`writePacked`, `funCompareSuffixes`, `funCalcSAiFromSA`. LE keeps native single-instruction load; only BE compiles take portable byte-wise path. Unit-tested via `test/unit/test_byteOrder.cpp` (BE algorithm validated on LE runner). Compile-only — no BE CI runner.
4. **Makefile ARM64/macOS parity** (#2694): arch-detected SIMD, Apple-clang libomp, `STARforMac` target, BSD date fallback. (CI uses CMake which already handles ARM64; Makefile path is NOT CI-tested.)

## Verified already-present (not redone)
- SJ memory leak #2676 — already in `outputSJ.cpp`.
- pubsetbuf/libc++ #2691 — already solved by `FixedStreamBuf.h`.

## Follow-ups / limitations
- Big-endian correctness unverified (no s390x/ppc64 in CI).
- CRAM adds one read+write pass over alignment output at finalization (acceptable; documented). Reference-based CRAM not implemented.
- Makefile changes are not exercised by CI.
