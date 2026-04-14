# build_clangcl.ps1 — Build STAR using portable toolchain (clang-cl + Ninja)
# Usage: powershell -ExecutionPolicy Bypass -File scripts\build_clangcl.ps1
# Requires: scripts\bootstrap_toolchain.ps1 run first, and VS Build Tools installed

param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$BuildType = "Release",
    [switch]$Clean,
    [switch]$FastMode
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$ToolchainDir = Join-Path $RepoRoot "toolchain"
$SourceDir = Join-Path $RepoRoot "source"

$BuildName = "build-clangcl"
if ($FastMode) { $BuildName = "build-clangcl-fast" }
$BuildDir = Join-Path $SourceDir $BuildName

# Verify toolchain
$cmakeExe = Join-Path $ToolchainDir "cmake\bin\cmake.exe"
$ninjaExe = Join-Path $ToolchainDir "ninja\ninja.exe"
$clangcl  = Join-Path $ToolchainDir "llvm\bin\clang-cl.exe"

foreach ($tool in @($cmakeExe, $ninjaExe, $clangcl)) {
    if (-not (Test-Path $tool)) {
        Write-Error "$tool not found. Run scripts\bootstrap_toolchain.ps1 first."
        exit 1
    }
}

# Find and source vcvars64 (needed for Windows SDK headers/libs)
$vcvarsPath = "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvarsPath)) {
    # Try common VS paths
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -property installationPath
        $vcvarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    }
}
if (-not (Test-Path $vcvarsPath)) {
    Write-Error "Cannot find vcvars64.bat. Install Visual Studio Build Tools."
    exit 1
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "[CLEAN] Removing $BuildDir"
    Remove-Item $BuildDir -Recurse -Force
}

# CMake args
$cmakeArgs = @(
    "-S", $SourceDir,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=$ninjaExe",
    "-DCMAKE_C_COMPILER=$clangcl",
    "-DCMAKE_CXX_COMPILER=$clangcl",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DZLIB_BUILD_EXAMPLES=OFF",
    "-DZLIB_BUILD_TESTING=OFF"
)
if ($FastMode) {
    $cmakeArgs += "-DSTAR_FAST_MODE=ON"
}

$configCmd = "`"$vcvarsPath`" >nul 2>&1 && `"$cmakeExe`" $($cmakeArgs -join ' ') 2>&1"
$buildCmd  = "`"$vcvarsPath`" >nul 2>&1 && `"$cmakeExe`" --build `"$BuildDir`" -j $env:NUMBER_OF_PROCESSORS 2>&1"

Write-Host "=== STAR clang-cl Build ==="
Write-Host "  Build type: $BuildType"
Write-Host "  Fast mode:  $FastMode"
Write-Host "  Output dir: $BuildDir"
Write-Host ""

# Configure
Write-Host "[CMAKE] Configuring..."
$output = cmd /c "call $configCmd"
if ($LASTEXITCODE -ne 0) {
    $output | Write-Host
    Write-Error "CMake configure failed"
    exit 1
}
$output | Select-Object -Last 5 | Write-Host

# Build
Write-Host "[BUILD] Compiling..."
$output = cmd /c "call $buildCmd"
if ($LASTEXITCODE -ne 0) {
    $output | Write-Host
    Write-Error "Build failed"
    exit 1
}

# Verify
$starExe = Join-Path $BuildDir "STAR.exe"
if ($FastMode) { $starExe = Join-Path $BuildDir "STARfast.exe" }
# The binary name depends on whether STAR_FAST_MODE renames it
$candidates = @(
    (Join-Path $BuildDir "STARfast.exe"),
    (Join-Path $BuildDir "STAR.exe")
)
$starExe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $starExe) {
    Write-Error "STAR binary not found in $BuildDir"
    exit 1
}

$version = & $starExe --version 2>&1
Write-Host ""
Write-Host "=== Build Successful ==="
Write-Host "  Binary:  $starExe"
Write-Host "  Version: $version"

# List bundled DLLs
$dlls = Get-ChildItem (Split-Path $starExe) -Filter "*.dll" -ErrorAction SilentlyContinue
if ($dlls) {
    Write-Host "  Bundled DLLs:"
    foreach ($dll in $dlls) {
        Write-Host "    - $($dll.Name) ($([math]::Round($dll.Length/1KB))KB)"
    }
}

Write-Host ""
Write-Host "  Run: $starExe --version"
