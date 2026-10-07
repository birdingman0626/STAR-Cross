# Building STAR-Cross

Run commands from the repository root unless noted otherwise. Use a C++17
compiler, CMake and Git. CMake downloads pinned dependencies, so an initial
build needs network access. Downloads are verified against archive hashes in
`dependencies.lock.json`. CUDA is not required for ordinary builds.

```sh
git clone https://github.com/birdingman0626/STAR-Cross.git
cd STAR-Cross
```

## Linux

Install GCC or Clang with OpenMP support, CMake and optionally Ninja. CMake
uses system zlib when available and otherwise builds its pinned fallback.

```sh
cmake -S source -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
./build/STAR --version
```

## macOS (Apple Silicon or Intel)

Use Homebrew GCC to provide OpenMP. Set both C and C++ compilers:

```sh
brew install gcc ninja
GCC_BIN=$(ls "$(brew --prefix gcc)"/bin/g++-* | sort -V | tail -1)
GCC_VER=$(basename "$GCC_BIN" | grep -oE '[0-9]+$')
cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$(brew --prefix gcc)/bin/gcc-${GCC_VER}" \
  -DCMAKE_CXX_COMPILER="$GCC_BIN"
cmake --build build --parallel 8
./build/STAR --version
```

CMake disables global AVX2 by default on every architecture. Adapter clipping uses
Parasail's platform-specific SIMD implementation.

## Windows (MSVC)

Install Visual Studio C++ Build Tools, CMake and Ninja. Open an x64 developer
command prompt, then run:

```bat
cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
build\STAR.exe --version
```

For Ninja builds, use a compiler diagnostic language/code page consistently at
configure and build time. If localized `showIncludes` output is not tracked,
reconfigure in a fresh build directory and confirm header edits rebuild consumers.
Use `VSLANG=1033` when the English compiler resources are installed.

For redistribution, include the required MSVC and OpenMP runtime DLLs alongside
`STAR.exe`; the GitHub release workflow handles this packaging.

### Portable clang-cl toolchain

An alternative bootstrap script downloads tools into `toolchain/` without a
system-wide installation. Run it with PowerShell 7 and follow the build commands
it prints:

```powershell
pwsh -File scripts/bootstrap_toolchain.ps1
```

This is a separate toolchain path, not the default MSVC release configuration.

## Build options

Pass options to the CMake configure command:

| Option | Default | Use |
| --- | --- | --- |
| `STAR_USE_AVX2` | OFF | Optional AVX2-only build; requires an AVX2 CPU |
| `STAR_CXX_STANDARD` | 17 | Set 20 for migration qualification |
| `STAR_LONG_READS` | OFF | Build the long-read variant |
| `STAR_POSIX_SHARED_MEM` | OFF | Unix-only POSIX shared-memory backend (`make POSIXSHARED`) |
| `STAR_BUILD_TESTS` | ON | Build the CTest unit suite |
| `STAR_ASAN` | OFF | Enable AddressSanitizer for debugging |
| `STAR_UBSAN` | OFF | Enable fail-fast UndefinedBehaviorSanitizer with GCC/Unix Clang |
| `USE_SYSTEM_HTSLIB` | OFF | Use system HTSlib via pkg-config instead of the bundled copy |
| `STAR_USE_LIBDEFLATE` | OFF | Use an installed libdeflate with bundled HTSlib |
| `STAR_ENABLE_CUDA` | OFF | Build the experimental CUDA adapter |
| `STAR_CAPTURE_SEEDS` | OFF | Enable bounded diagnostic seed capture; not for performance measurement |

`STAR_USE_LIBDEFLATE` requires its development headers/library and cannot be
combined with `USE_SYSTEM_HTSLIB`. Neither option guarantees a speedup.
For CUDA requirements and validation, use the [GPU experiment guide](GPU_EXPERIMENT.md).

Configure-aware flags support Ninja and multi-configuration generators. Build
timestamps use `SOURCE_DATE_EPOCH` when supplied and otherwise say unspecified;
hostnames and working directories are not embedded as build provenance.

From `source/`, CMake >= 3.21 also supports `cmake --preset release`,
`cmake --build --preset release`, and `ctest --preset release`. The `debug`,
`sanitized` (GCC/Unix Clang) and `cxx20` presets use separate build directories.

GNU Make is a compatibility frontend to CMake: `make STAR`, `make STARlong`,
`make gdb`, or `make gdb-long`, with `JOBS`, `CXX_STANDARD` and `CMAKE_ARGS`.
It no longer maintains a separate source list. Full-static legacy targets report
that a separately qualified toolchain is required instead of producing a binary
with a misleading static name.

Install only this product with `cmake --install build --config Release
--component STAR --prefix staging`. This avoids installing unbuilt dependency
applications. It installs the build record and dependency notices as well.
See [dependency maintenance](DEPENDENCIES.md) and [C++20 checklist](CXX20_UPGRADE_PLAN.md).

## Tests

After building with `STAR_BUILD_TESTS=ON`:

```sh
ctest --test-dir build/test --no-tests=error --output-on-failure
```

The Python harness suite includes Linux/WSL-only Bash and executable fixtures:

```sh
python3 -m unittest discover -s scripts -p 'test_*py'
```

For actual alignment/count comparisons and scientific validation boundaries,
see [CPU validation](CPU_VALIDATION.md).

The independent maintenance checks are recorded in
[the maintenance audit](PROJECT_MAINTENANCE_20261007.md). Exercise the real
embedded server with `python scripts/test_webui.py --star-exe <binary>`.
Full CLI sanitizer integration disables leak detection for existing
process-lifetime allocations; sanitized unit tests retain leak detection.
CPU ASan/UBSan do not validate CUDA device accesses.
