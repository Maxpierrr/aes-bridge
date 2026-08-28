# SPDX-License-Identifier: GPL-3.0-only
param(
    [Parameter(Mandatory = $true)]
    [string]$Engine
)

$ErrorActionPreference = "Stop"

function Get-EngineStatus {
    $statusInfo = New-Object System.Diagnostics.ProcessStartInfo
    $statusInfo.FileName = $Engine
    $statusInfo.Arguments = "--status"
    $statusInfo.UseShellExecute = $false
    $statusInfo.RedirectStandardOutput = $true
    $statusInfo.RedirectStandardError = $true
    $statusProcess = New-Object System.Diagnostics.Process
    $statusProcess.StartInfo = $statusInfo
    if (!$statusProcess.Start()) { return $null }
    $json = $statusProcess.StandardOutput.ReadToEnd()
    $statusProcess.WaitForExit()
    if ($statusProcess.ExitCode -ne 0) { return $null }
    try { return $json | ConvertFrom-Json } catch { return $null }
}
$arguments = @(
    "--run",
    "--interface-address", "127.0.0.1",
    "--rx-group", "127.0.0.1",
    "--tx-group", "127.0.0.1",
    "--rx-port", "54900",
    "--tx-port", "54901",
    "--channels-per-stream", "2",
    "--core-audio-start-channel", "1",
    "--duration", "10",
    "--no-sap",
    "--no-ptp"
)

$startInfo = New-Object System.Diagnostics.ProcessStartInfo
$startInfo.FileName = $Engine
$startInfo.Arguments = $arguments -join " "
$startInfo.UseShellExecute = $false
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true
$backend = New-Object System.Diagnostics.Process
$backend.StartInfo = $startInfo
if (!$backend.Start()) {
    throw "Windows backend process could not be started."
}
try {
    $status = $null
    for ($attempt = 0; $attempt -lt 30 -and $null -eq $status; $attempt++) {
        Start-Sleep -Milliseconds 100
        $candidate = Get-EngineStatus
        if ($null -ne $candidate -and $candidate.engineRunning -and $candidate.virtualChannels -eq 64 -and $candidate.activeStreamCount -eq 1) {
            $status = $candidate
        }
    }
    if ($null -eq $status) {
        $details = if ($backend.HasExited) {
            " Exit=$($backend.ExitCode); stdout=$($backend.StandardOutput.ReadToEnd()); stderr=$($backend.StandardError.ReadToEnd())"
        } else { " Process still running." }
        throw "Windows backend did not publish valid stereo planet status.$details"
    }
    if (!$backend.WaitForExit(15000)) {
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
