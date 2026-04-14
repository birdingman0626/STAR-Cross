# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 2.7.11c (this fork) | Yes — active development |
| 2.7.11b (upstream)  | See [alexdobin/STAR](https://github.com/alexdobin/STAR) |
| < 2.7.11b           | No |

This repository is a performance and portability fork of [STAR](https://github.com/alexdobin/STAR).
Security fixes that apply to the upstream aligner core should be reported to both this repository
and the upstream project.

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Report security issues privately using one of the following methods:

1. **GitHub private security advisory** (preferred):
   Go to [Security → Advisories → New draft advisory](../../security/advisories/new)
   and submit a draft advisory. Maintainers will be notified immediately.

2. **Email**: Contact the repository owner directly through the GitHub profile
   ([birdingman0626](https://github.com/birdingman0626)).

Please include:
- A description of the vulnerability and its potential impact
- Steps to reproduce or a proof-of-concept (if available)
- Affected version(s) and platforms
- Any suggested mitigations

## Response Timeline

| Stage | Target |
|-------|--------|
| Initial acknowledgement | 48 hours |
| Triage and severity assessment | 5 business days |
| Fix or mitigation | Depends on severity — critical issues prioritised |
| Public disclosure | Coordinated with reporter after fix is available |

## Scope

**In scope:**
- Memory safety issues in STAR's own C++ code (`source/` excluding `source/htslib/`, `source/opal/`, `source/SimpleGoodTuring/`)
- Incorrect output that could silently corrupt downstream analysis
- Windows portability layer (`source/wincompat.h`) security issues

**Out of scope (report to upstream instead):**
- Issues in bundled third-party libraries:
  - **HTSlib** → [samtools/htslib](https://github.com/samtools/htslib/security)
  - **Opal** → [Martinsos/opal](https://github.com/Martinsos/opal)
- Issues in the reference genome or annotation files (not part of this codebase)
- Denial-of-service via crafted input files (STAR is a bioinformatics CLI tool,
  not a network service; malicious input files are considered out of scope)

## Vendored Dependencies

STAR bundles the following third-party libraries. Known issues in these libraries
are tracked upstream and are excluded from this repository's CodeQL scanning:

| Library | Version | Upstream |
|---------|---------|----------|
| HTSlib  | 1.21    | [samtools/htslib](https://github.com/samtools/htslib) |
| Opal    | (bundled) | [Martinsos/opal](https://github.com/Martinsos/opal) |
| SIMDe   | (via opal) | [simd-everywhere/simde](https://github.com/simd-everywhere/simde) |
| SimpleGoodTuring | (bundled) | N/A |
