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

$backend = Start-Process -FilePath $Engine -ArgumentList $arguments -PassThru -NoNewWindow
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
        throw "Le backend Windows n'a pas publié un statut 64 canaux valide."
    }
    Wait-Process -Id $backend.Id -Timeout 8
    $backend.Refresh()
    if ($backend.ExitCode -ne 0) {
        throw "Le backend Windows s'est arrêté avec le code $($backend.ExitCode)."
    }
    Write-Output "AES Bridge Windows CLI lifecycle passed"
}
finally {
    if (!$backend.HasExited) {
        Stop-Process -Id $backend.Id -Force
    }
}
