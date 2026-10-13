$url = 'https://dl.google.com/android/repository/android-ndk-r27-windows.zip'
$dest = 'C:\Android\ndk\android-ndk-r27.zip'
$extractDir = 'C:\Android\ndk'

Write-Host 'Creating directory...' -ForegroundColor Cyan
New-Item -ItemType Directory -Path $extractDir -Force | Out-Null

Write-Host 'Downloading NDK r27 (~1.2 GB)...' -ForegroundColor Cyan
try {
    $wc = New-Object System.Net.WebClient
    $wc.DownloadFile($url, $dest)
    Write-Host 'Download complete!' -ForegroundColor Green
} catch {
    Write-Host ('Download failed: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}

Write-Host 'Extracting...' -ForegroundColor Cyan
Expand-Archive -Path $dest -DestinationPath $extractDir -Force

Write-Host 'Cleaning up zip...' -ForegroundColor Cyan
Remove-Item $dest -Force

$ndkPath = Join-Path $extractDir 'android-ndk-r27'
if (Test-Path $ndkPath) {
    Write-Host ('NDK installed at: ' + $ndkPath) -ForegroundColor Green
    Write-Host 'Set this in your session:' -ForegroundColor Yellow
    Write-Host ('  $env:ANDROID_NDK_HOME = "' + $ndkPath + '"') -ForegroundColor Yellow
} else {
    Write-Host 'Extraction may have different folder name:' -ForegroundColor Yellow
    Get-ChildItem $extractDir -Directory | ForEach-Object { Write-Host ('  ' + $_.FullName) }
}