# HTSlib Upgrade Plan: 1.3 → 1.21

## Status: Evaluated and Deferred
Last updated: 2026-04-13

## Decision

After evaluation, the upgrade is deferred. Reasons:

1. **Security risk is minimal**: STAR reads trusted input (genome index, FASTQ). No CRAM parsing, no network I/O, no untrusted BAM input. Attack surface is negligible.
2. **API stability**: STAR uses only stable htslib APIs (bgzf_*, bam_*, sam_hdr_*). These haven't broken across versions.
3. **Current version works**: HTSlib 1.3 with Windows patches produces byte-identical output verified against Linux.
4. **Upgrade risk is high**: 8+ years of changes, new dependencies (liblzma, libbz2, libdeflate), API migration needed.
5. **HTSlib 1.21 Windows support is partial**: Only 5 files have `_WIN32` guards. Our patches would still be needed.

## When to Revisit

- If a CVE is found in htslib 1.3's bgzf/BAM parsing
- If CRAM output support is needed (PR #2670)
- If a new htslib API feature is required
