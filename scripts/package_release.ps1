# package_release.ps1 — Package STAR binary + DLLs into a release archive
# Usage: powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1 -BuildDir source\build-clangcl
# Produces: release\STAR-v2.7.11b-windows-x64.zip (or STARfast variant)

param(
    [Parameter(Mandatory)]
    [string]$BuildDir,
    [string]$OutputDir = "release",
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$BuildDir = Join-Path $RepoRoot $BuildDir

# Auto-detect version from binary
$starExe = Get-ChildItem $BuildDir -Filter "STAR*.exe" | Where-Object { $_.Name -match "^STAR(fast)?\.exe$" } | Select-Object -First 1
if (-not $starExe) {
    Write-Error "No STAR.exe or STARfast.exe found in $BuildDir"
    exit 1
}

if (-not $Version) {
    $Version = & $starExe.FullName --version 2>&1
    $Version = $Version.Trim()
}

# Determine variant name
$variant = "STAR"
if ($starExe.Name -eq "STARfast.exe") { $variant = "STARfast" }

$archiveName = "$variant-v$Version-windows-x64"
$stageDir = Join-Path $RepoRoot "$OutputDir\$archiveName"
$archivePath = Join-Path $RepoRoot "$OutputDir\$archiveName.zip"

# Create staging directory
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

Write-Host "=== Packaging $variant v$Version ==="

# Copy binary
Copy-Item $starExe.FullName -Destination $stageDir
Write-Host "  + $($starExe.Name)"

# Copy all DLLs from build directory
$dlls = Get-ChildItem $BuildDir -Filter "*.dll"
foreach ($dll in $dlls) {
    Copy-Item $dll.FullName -Destination $stageDir
    Write-Host "  + $($dll.Name) ($([math]::Round($dll.Length/1KB))KB)"
}

# Copy PDB if exists (useful for crash debugging)
$pdb = Get-ChildItem $BuildDir -Filter "$($starExe.BaseName).pdb" -ErrorAction SilentlyContinue
if ($pdb) {
    Copy-Item $pdb.FullName -Destination $stageDir
    Write-Host "  + $($pdb.Name) (debug symbols)"
}

# Verify the binary runs from the staged directory
$testResult = & "$stageDir\$($starExe.Name)" --version 2>&1
if ($testResult -ne $Version) {
    Write-Error "Staged binary failed to run. Missing DLLs?"
    Write-Host "  Expected: $Version"
    Write-Host "  Got: $testResult"
    exit 1
}
Write-Host "  Verified: binary runs from staged directory"

# Create ZIP archive
if (Test-Path $archivePath) { Remove-Item $archivePath -Force }
Compress-Archive -Path "$stageDir\*" -DestinationPath $archivePath
$archiveSize = [math]::Round((Get-Item $archivePath).Length / 1MB, 1)

Write-Host ""
Write-Host "=== Package Ready ==="
Write-Host "  Archive: $archivePath ($archiveSize MB)"
Write-Host "  Contents:"
Get-ChildItem $stageDir | ForEach-Object { Write-Host "    $($_.Name) ($([math]::Round($_.Length/1KB))KB)" }
Write-Host ""
Write-Host "  Upload to GitHub Release:"
Write-Host "    gh release upload v$Version `"$archivePath`""

# Cleanup staging directory
Remove-Item $stageDir -Recurse -Force
