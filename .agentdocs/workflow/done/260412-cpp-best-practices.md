# C/C++ Best Practice Improvements for STAR

## Status: Mostly Complete
Last updated: 2026-04-13

## Completed

- [x] Compiler warnings: `/W4` on MSVC with specific suppressions for upstream code noise
- [x] Sanitizer coverage: ASAN support for both MSVC and GCC/Clang
- [x] CMake: `compile_commands.json` export, `enable_testing()`, version check test
- [x] Makefile OBJECTS bug: fixed 7 `.cpp` → `.o` entries
- [x] .gitignore: added CMake build dirs, binaries, editor artifacts
- [x] Code formatting: `.clang-format` and `.editorconfig` added
- [x] Static analysis: `.clang-tidy` config with conservative rules
- [x] CI/CD: GitHub Actions workflow (Linux GCC/Clang, macOS, Windows MSVC)
- [x] C++17 standard upgrade (both CMake and Makefile)
- [x] Docker support: multi-stage `Dockerfile`
- [x] Testing: CTest integration with version check test

## Deferred

- [ ] Memory management modernization (new code should use smart pointers; no bulk rewrite)
- [ ] Source directory reorganization (too disruptive for now, not recommended)
- [ ] Full testing framework (Catch2/GoogleTest) — needs test data bundled in repo
