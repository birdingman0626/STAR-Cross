# STAR Windows Port

## Status: Complete
Last updated: 2026-04-13

---

## Windows Port [COMPLETE]

### Done
- [x] `wincompat.h` - POSIX shim layer (pthread, sleep, mkdir, kill, mmap stubs)
- [x] `FixedStreamBuf.h` - Cross-platform streambuf replacing broken `pubsetbuf` on MSVC
- [x] `CMakeLists.txt` - CMake build system (MSVC/GCC/Clang, auto-fetches zlib)
- [x] `htslib/win32_compat.h` + stub headers for htslib C code
- [x] FIFO replacement with temp files + system() on Windows
- [x] `sysRemoveDir.cpp` Windows implementation (FindFirstFile/RemoveDirectory)
- [x] `systemFunctions.cpp` Windows memory reporting (GetProcessMemoryInfo)
- [x] VLA removal (std::vector replacements) for MSVC C++ compliance
- [x] OpenMP signed loop variable fixes (MSVC OpenMP 2.0)
- [x] Missing `#include <numeric>` for MSVC
- [x] `__uint128_t` replacement with byte-level access
- [x] opal/SIMDe MSVC compatibility (VLA removal, alignment, isnan)
- [x] Bug fix: initialize all 11 pthread_mutex_t (upstream only did 8)
- [x] Bug fix: `stitchAlignToTranscript.h` const mismatch
- [x] Bug fix: `pubsetbuf` no-op on MSVC (FixedStreamBuf replaces it)
- [x] Verified: 434M reads produce byte-identical results to Linux orig_count
- [x] `/O2` optimization verified working

### Known Windows Limitations
- `--genomeLoad` shared memory not supported (NoSharedMemory only)
- `--readFilesCommand` uses temp files instead of FIFO pipes
- Performance ~3.5x slower than Linux (iostream overhead, MSVC allocator)

### CUDA Evaluation (Rejected)
GPU acceleration was prototyped and tested on RTX PRO 6000 Blackwell (96GB VRAM, 188 SMs).
Conclusion: not worth it for STAR. The UMI kernel processed only 98K UMIs across 774 launches
(avg 127 UMIs/launch) - too small for GPU overhead. The mapping bottleneck (88% of runtime)
is memory-bound SA search, not compute-bound. Focus should be on CPU performance optimization.
