## Current Task Documentation
`workflow/260412-windows-port-and-cuda.md` - Windows MSVC port + CUDA acceleration plan and status
`workflow/260412-htslib-upgrade.md` - HTSlib 1.3 → 1.21 upgrade plan with Windows patch re-application
`workflow/260412-cpp-best-practices.md` - C/C++ best practice improvements audit and action items
`workflow/260412-release-validation-test.md` - Release validation: STARsolo count comparison against orig_count

## Technical Notes
- STAR uses `pubsetbuf` on `istringstream` to use external buffers; MSVC ignores this. Fixed with `FixedStreamBuf.h`.
- Upstream STAR has 3 uninitialized `pthread_mutex_t` members that happen to work on Linux (zero-init = valid). Crashes on Windows.
- STAR's VLAs and `__uint128_t` usage requires platform-specific replacements on MSVC.
