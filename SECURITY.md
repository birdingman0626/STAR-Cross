# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| Current STAR-Cross commit-hash release | Yes — active development |
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

**Also report to the dependency upstream where applicable:**
- Issues in bundled third-party libraries (reachable findings are reviewed and
  may receive bounded local patches here):
  - **HTSlib** → [samtools/htslib](https://github.com/samtools/htslib/security)
- Issues in dependencies fetched via CMake FetchContent:
  - **Parasail** → [jeffdaily/parasail](https://github.com/jeffdaily/parasail)
- Issues in the reference genome or annotation files (not part of this codebase)
- Expected resource exhaustion from legitimately large input is not by itself a
  security defect. Memory corruption and invalid length checks on malformed
  inputs are in scope even though STAR is primarily a local CLI.

## Vendored Dependencies

STAR bundles the following third-party libraries. Known issues in these libraries
are tracked upstream and reviewed individually in this repository's CodeQL
scanning; dependencies are not blanket-excluded:

| Library | Version | Upstream |
|---------|---------|----------|
| HTSlib  | 1.24    | [samtools/htslib](https://github.com/samtools/htslib) |
| SimpleGoodTuring | (bundled) | N/A |

Additionally, **Parasail v2.6.2** is fetched at build time via CMake FetchContent
([jeffdaily/parasail](https://github.com/jeffdaily/parasail)) for adapter clipping.
