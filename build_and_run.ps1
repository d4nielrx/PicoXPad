# PicoXPad build script (Windows) - builds the daemon and the Magisk module ZIP.
# Requires Android NDK r27 (or set ANDROID_NDK_HOME to your NDK).
param(
    [string]$NdkPath = $(if ($env:ANDROID_NDK_HOME) { $env:ANDROID_NDK_HOME } else { "C:\Android\ndk\android-ndk-r27" })
)

$ErrorActionPreference = "Stop"
$scriptDir = $PSScriptRoot
$apiLevel = 29
$target = "aarch64-linux-android$apiLevel"

$toolchainDir = Join-Path $NdkPath "toolchains\llvm\prebuilt\windows-x86_64"
$cc = Join-Path $toolchainDir "bin\clang++.exe"

if (-not (Test-Path $cc)) {
    Write-Host "Compiler not found: $cc" -ForegroundColor Red
    Write-Host "Install NDK r27 (see download_ndk.ps1) or pass -NdkPath" -ForegroundColor Yellow
    exit 1
}

Write-Host "Using NDK: $NdkPath (target: $target)" -ForegroundColor Cyan

# Clean build dir
$buildDir = Join-Path $scriptDir "build"
$outDir = Join-Path $scriptDir "out"
if (Test-Path $buildDir) { Remove-Item $buildDir -Recurse -Force }
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null

Write-Host "Compiling picoxpad_daemon..." -ForegroundColor Cyan
$outputFile = Join-Path $buildDir "picoxpad_daemon"

& $cc @(
    "-target", $target,
    "-std=c++17",
    "-O2",
    "-Wall",
    "-Wextra",
    "-D_GNU_SOURCE",
    (Join-Path $scriptDir "picoxpad_daemon.cpp"),
    "-o", $outputFile,
    "-llog",
    "-Wl,--build-id=sha1"
)

if ($LASTEXITCODE -ne 0) {
    Write-Host "Compilation failed!" -ForegroundColor Red
    exit 1
}

# Strip binary
$strip = Join-Path $toolchainDir "bin\llvm-strip.exe"
if (Test-Path $strip) { & $strip $outputFile 2>$null }

Write-Host "Build successful: $outputFile ($((Get-Item $outputFile).Length) bytes)" -ForegroundColor Green

# Keep a copy under out/
$abiOutDir = Join-Path $outDir "arm64-v8a"
New-Item -ItemType Directory -Path $abiOutDir -Force | Out-Null
Copy-Item $outputFile -Destination $abiOutDir -Force

# ---- Create Magisk module ZIP ----
# NOTE: the binary MUST sit at the ZIP ROOT - subdirectories do not extract
# properly on this device's Magisk.
$version = (Select-String -Path (Join-Path $scriptDir "module.prop") -Pattern '^version=(.+)$').Matches[0].Groups[1].Value
$zipName = "PicoXPad-v$version.zip"
$zipPath = Join-Path $scriptDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in @("module.prop", "service.sh", "post-fs-data.sh", "sepolicy.rule")) {
        $entry = $zip.CreateEntry($f)
        $stream = $entry.Open()
        $bytes = [System.IO.File]::ReadAllBytes((Join-Path $scriptDir $f))
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Close()
    }
    $entry = $zip.CreateEntry("picoxpad_daemon")
    $stream = $entry.Open()
    $bytes = [System.IO.File]::ReadAllBytes($outputFile)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Close()
} finally {
    $zip.Dispose()
}

Write-Host "Module ZIP created: $zipPath" -ForegroundColor Green
Write-Host ""
Write-Host "Installation:" -ForegroundColor Cyan
Write-Host "1. Transfer $zipName to your Pico 4"
Write-Host "2. Magisk app -> Modules -> Install from storage -> select ZIP -> reboot"
Write-Host "3. Hold BOTH GRIPS ~0.5s on the two controllers to toggle Xbox mode" -ForegroundColor Yellow
