# SPDX-License-Identifier: GPL-3.0-only
param(
    [Parameter(Mandatory = $true)]
    [string]$Engine
)

$ErrorActionPreference = "Stop"
$arguments = @(
    "--run",
    "--interface-address", "127.0.0.1",
    "--rx-group", "127.0.0.1",
    "--tx-group", "127.0.0.1",
    "--rx-port", "54900",
    "--tx-port", "54901",
    "--duration", "3",
    "--no-sap",
    "--no-ptp"
)

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $Engine
$startInfo.Arguments = $arguments -join " "
$startInfo.UseShellExecute = $false
$backend = New-Object System.Diagnostics.Process
$backend.StartInfo = $startInfo
if (!$backend.Start()) {
    throw "Windows backend process could not be started."
}
try {
    $status = $null
    for ($attempt = 0; $attempt -lt 30 -and $null -eq $status; $attempt++) {
        Start-Sleep -Milliseconds 100
        $json = & $Engine --status 2>$null
        if ($LASTEXITCODE -eq 0) {
            $candidate = $json | ConvertFrom-Json
            if ($candidate.engineRunning -and $candidate.virtualChannels -eq 64) {
                $status = $candidate
            }
        }
    }
    if ($null -eq $status) {
        throw "Windows backend did not publish valid 64-channel status."
    }
    if (!$backend.WaitForExit(8000)) {
        throw "Windows backend did not stop after its configured duration."
    }
    if ($backend.ExitCode -ne 0) {
        throw "Windows backend exited with code $($backend.ExitCode)."
    }
    Write-Output "AES Bridge Windows CLI lifecycle passed"
}
finally {
    if (!$backend.HasExited) {
        Stop-Process -Id $backend.Id -Force
    }
}
