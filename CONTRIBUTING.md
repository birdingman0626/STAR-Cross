# Contributing to STAR (Community Fork)

## This Fork

This is a community fork of [alexdobin/STAR](https://github.com/alexdobin/STAR). Report issues and submit PRs to [birdingman0626/STAR](https://github.com/birdingman0626/STAR/issues).

## Build Requirements

- **Linux/macOS:** GCC 7+ or Clang 5+, CMake 3.15+, zlib
- **Windows:** MSVC 2019+ or Intel ICX 2024+, CMake 3.15+

```bash
# CMake (all platforms)
cd source && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Make (Linux only)
cd source && make STAR
```

## Before Submitting a PR

1. **Build with both CMake and Make** (if modifying source file lists)
2. **Run smoke test** (1M reads) — output must match reference
3. **No new warnings** on MSVC `/W4` or GCC `-Wall -Wextra`
4. **Output compatibility is mandatory** — changes that alter integer count matrices require explicit justification

## Dependency Inventory

| Dependency | Location | Role | Replaceable? | Allowed includers |
|---|---|---|---|---|
| **HTSlib 1.21** | `source/htslib/` (bundled) | BAM/SAM/CRAM I/O | No (deep, cross-cutting) | Any source file via `htslib/*.h` |
| **Parasail v2.6.2** | CMake FetchContent | Adapter clipping alignment | Yes (narrow scope) | `ClipCR4.h/.cpp` only |
| **SimpleGoodTuring** | `source/SimpleGoodTuring/` (bundled) | EmptyDrops ambient smoothing | No (tiny, stable algorithm) | `SoloFeature_emptyDrops_CR.cpp` only |
| **zlib** | System or CMake FetchContent | Compression | No (standard library) | Via HTSlib; STAR does not include directly |

### Vendored Code Policy

`source/htslib/` and `source/SimpleGoodTuring/` are bundled dependencies. Patches must:
- Document the upstream source and version
- Explain why the patch is needed
- Be verified with a smoke test

**Rule:** Do not add new direct includes from vendored directories without justification. Keep adapter clipping behind `ClipCR4`, SGT usage inside `SoloFeature_emptyDrops_CR.cpp`.

## Code Style

- C++17 standard
- `.clang-format` and `.editorconfig` define formatting
- Run `pre-commit install` for automatic checks
