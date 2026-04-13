# bootstrap_toolchain.ps1 — Download and cache portable build tools
# Usage: powershell -ExecutionPolicy Bypass -File scripts\bootstrap_toolchain.ps1
# Re-running is idempotent — skips already-cached tools.

param(
    [switch]$ForceRefresh,
    [string]$ToolchainDir = "$PSScriptRoot\..\toolchain"
)

$ErrorActionPreference = "Stop"
$ToolchainDir = (Resolve-Path -LiteralPath $ToolchainDir -ErrorAction SilentlyContinue) ?? (New-Item -ItemType Directory -Path $ToolchainDir -Force).FullName

# Tool versions — update these to upgrade
$CMakeVersion = "4.0.3"
$NinjaVersion = "1.12.1"
$LLVMVersion  = "20.1.7"

$CMakeUrl = "https://github.com/Kitware/CMake/releases/download/v${CMakeVersion}/cmake-${CMakeVersion}-windows-x86_64.zip"
$NinjaUrl = "https://github.com/ninja-build/ninja/releases/download/v${NinjaVersion}/ninja-win.zip"
$LLVMUrl  = "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVMVersion}/clang+llvm-${LLVMVersion}-x86_64-pc-windows-msvc.tar.xz"

$CacheDir = Join-Path $ToolchainDir "cache"
New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ToolchainDir "cmake") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ToolchainDir "ninja") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $ToolchainDir "llvm") -Force | Out-Null

function Download-AndExtract {
    param(
        [string]$Name,
        [string]$Url,
        [string]$Archive,
        [string]$DestDir,
        [string]$CheckExe,
        [string]$ExtractNested  # if archive contains a top-level folder to flatten
    )

    if ((Test-Path $CheckExe) -and -not $ForceRefresh) {
        Write-Host "[SKIP] $Name already cached at $CheckExe"
        return
    }

    if ($ForceRefresh -and (Test-Path $DestDir)) {
        Write-Host "[CLEAN] Removing old $Name..."
        Get-ChildItem $DestDir | Remove-Item -Recurse -Force
    }

    $archivePath = Join-Path $CacheDir $Archive
    if (-not (Test-Path $archivePath) -or $ForceRefresh) {
        Write-Host "[DOWNLOAD] $Name from $Url"
        curl.exe -L -o $archivePath $Url --progress-bar
        if ($LASTEXITCODE -ne 0) { throw "Failed to download $Name" }
    } else {
        Write-Host "[CACHED] $Name archive already downloaded"
    }

    Write-Host "[EXTRACT] $Name..."
    if ($Archive -like "*.tar.xz") {
        # Use Git Bash's tar+xz for .tar.xz
        $bashTar = "cd '$($CacheDir -replace '\\','/')' && xz -d -k -f '$Archive' && tar xf '$($Archive -replace '\.xz$','')' && rm -f '$($Archive -replace '\.xz$','')'"
        bash -c $bashTar
        if ($LASTEXITCODE -ne 0) { throw "Failed to extract $Name" }
    } else {
        Expand-Archive -Path $archivePath -DestinationPath (Join-Path $CacheDir "extract-tmp") -Force
    }

    # Move contents to destination
    if ($ExtractNested) {
        $srcDir = Join-Path $CacheDir $ExtractNested
        if (Test-Path $srcDir) {
            Copy-Item -Path "$srcDir\*" -Destination $DestDir -Recurse -Force
            Remove-Item $srcDir -Recurse -Force
        }
    } else {
        $tmpDir = Join-Path $CacheDir "extract-tmp"
        $nested = Get-ChildItem $tmpDir | Select-Object -First 1
        if ($nested) {
            Copy-Item -Path "$($nested.FullName)\*" -Destination $DestDir -Recurse -Force
        }
        Remove-Item $tmpDir -Recurse -Force
    }

    if (Test-Path $CheckExe) {
        Write-Host "[OK] $Name installed"
    } else {
        throw "$Name extraction failed — $CheckExe not found"
    }
}

Write-Host "=== STAR Toolchain Bootstrap ==="
Write-Host "Toolchain directory: $ToolchainDir"
Write-Host ""

# CMake
Download-AndExtract `
    -Name "CMake $CMakeVersion" `
    -Url $CMakeUrl `
    -Archive "cmake.zip" `
    -DestDir (Join-Path $ToolchainDir "cmake") `
    -CheckExe (Join-Path $ToolchainDir "cmake\bin\cmake.exe")

# Ninja
Download-AndExtract `
    -Name "Ninja $NinjaVersion" `
    -Url $NinjaUrl `
    -Archive "ninja.zip" `
    -DestDir (Join-Path $ToolchainDir "ninja") `
    -CheckExe (Join-Path $ToolchainDir "ninja\ninja.exe")

# LLVM/Clang
Download-AndExtract `
    -Name "LLVM $LLVMVersion" `
    -Url $LLVMUrl `
    -Archive "llvm.tar.xz" `
    -DestDir (Join-Path $ToolchainDir "llvm") `
    -CheckExe (Join-Path $ToolchainDir "llvm\bin\clang-cl.exe") `
    -ExtractNested "clang+llvm-${LLVMVersion}-x86_64-pc-windows-msvc"

# Verification
Write-Host ""
Write-Host "=== Verification ==="
$cmakeExe = Join-Path $ToolchainDir "cmake\bin\cmake.exe"
$ninjaExe = Join-Path $ToolchainDir "ninja\ninja.exe"
$clangExe = Join-Path $ToolchainDir "llvm\bin\clang-cl.exe"

Write-Host "CMake:  $(& $cmakeExe --version | Select-Object -First 1)"
Write-Host "Ninja:  $(& $ninjaExe --version)"
Write-Host "Clang:  $((& $clangExe --version 2>&1) | Select-Object -First 1)"

Write-Host ""
Write-Host "=== Build Commands ==="
Write-Host ""
Write-Host "  # clang-cl + Ninja build:"
Write-Host "  $cmakeExe -S source -B source\build-clangcl -G Ninja ``"
Write-Host "    -DCMAKE_MAKE_PROGRAM=$ninjaExe ``"
Write-Host "    -DCMAKE_C_COMPILER=$clangExe ``"
Write-Host "    -DCMAKE_CXX_COMPILER=$clangExe ``"
Write-Host "    -DCMAKE_BUILD_TYPE=Release"
Write-Host "  $cmakeExe --build source\build-clangcl"
Write-Host ""
Write-Host "  # MSVC + Ninja build (requires VS Developer environment):"
Write-Host "  $cmakeExe -S source -B source\build-msvc -G Ninja ``"
Write-Host "    -DCMAKE_MAKE_PROGRAM=$ninjaExe ``"
Write-Host "    -DCMAKE_BUILD_TYPE=Release"
Write-Host "  $cmakeExe --build source\build-msvc"
Write-Host ""
Write-Host "=== Toolchain ready ==="
