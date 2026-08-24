# SPDX-License-Identifier: GPL-3.0-only
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,
    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"
$projectDir = Split-Path -Parent $PSScriptRoot
$backend = Join-Path $BuildDir "aes-bridge-windows-backend.exe"
$selfTest = Join-Path $BuildDir "AESBridgeWindowsBackendSelfTest.exe"
if (!(Test-Path -PathType Leaf $backend)) { throw "Windows backend missing: $backend" }
if (!(Test-Path -PathType Leaf $selfTest)) { throw "Windows self-test missing: $selfTest" }

$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("aes-bridge-package-" + [guid]::NewGuid())
$packageRoot = Join-Path $stage "AES Bridge Windows backend x64"
$archive = Join-Path $OutputDir "AES-Bridge-Windows-backend-x64.zip"
$checksum = "$archive.sha256"
try {
    New-Item -ItemType Directory -Path $packageRoot, $OutputDir -Force | Out-Null
    Copy-Item $backend, $selfTest -Destination $packageRoot
    Copy-Item (Join-Path $projectDir "Packaging/Windows/README.txt") -Destination $packageRoot
    Copy-Item (Join-Path $projectDir "Docs/WINDOWS_BACKEND.md") -Destination $packageRoot
    Copy-Item (Join-Path $projectDir "LICENSE"), (Join-Path $projectDir "THIRD_PARTY_NOTICES.md") -Destination $packageRoot
    if (Test-Path $archive) { Remove-Item $archive }
    if (Test-Path $checksum) { Remove-Item $checksum }
    Compress-Archive -Path $packageRoot -DestinationPath $archive
    $hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    Set-Content -Path $checksum -Value "$hash  $(Split-Path -Leaf $archive)" -NoNewline
    Write-Output "Package created: $archive"
}
finally {
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
}
