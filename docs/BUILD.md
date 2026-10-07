# Building STAR-Cross

Run commands from the repository root unless noted otherwise. Use a C++17
compiler, CMake and Git. CMake downloads pinned dependencies, so an initial
build needs network access. CUDA is not required for ordinary builds.

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

CMake disables AVX2 by default on non-x86 architectures. Adapter clipping uses
Parasail's platform-specific SIMD implementation.

## Windows (MSVC)

Install Visual Studio C++ Build Tools, CMake and Ninja. Open an x64 developer
command prompt, then run:

```bat
cmake -S source -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
build\STAR.exe --version
```

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
| `STAR_USE_AVX2` | ON on x86-64, OFF elsewhere | Set OFF for x86 CPUs without AVX2 |
| `STAR_LONG_READS` | OFF | Build the long-read variant |
| `STAR_BUILD_TESTS` | ON | Build the CTest unit suite |
| `STAR_ASAN` | OFF | Enable AddressSanitizer for debugging |
| `USE_SYSTEM_HTSLIB` | OFF | Use system HTSlib via pkg-config instead of the bundled copy |
| `STAR_USE_LIBDEFLATE` | OFF | Use an installed libdeflate with bundled HTSlib |
| `STAR_ENABLE_CUDA` | OFF | Build the experimental CUDA adapter |
| `STAR_CAPTURE_SEEDS` | OFF | Enable bounded diagnostic seed capture; not for performance measurement |

`STAR_USE_LIBDEFLATE` requires its development headers/library and cannot be
combined with `USE_SYSTEM_HTSLIB`. Neither option guarantees a speedup.
For CUDA requirements and validation, use the [GPU experiment guide](GPU_EXPERIMENT.md).

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
