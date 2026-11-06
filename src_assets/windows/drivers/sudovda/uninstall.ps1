<#
.SYNOPSIS
Removes the SudoVDA virtual display device node (idempotent).
#>
$ErrorActionPreference = 'Stop'

$HardwareId = 'root\sudomaker\sudovda'
$ClassGuid = '4D36E968-E325-11CE-BFC1-08002BE10318'
$Nefcon = Join-Path $PSScriptRoot 'nefconc.exe'

if (-not (Test-Path -LiteralPath $Nefcon)) {
    Write-Warning "nefconc.exe not found at $Nefcon, nothing to remove"
    exit 0
}

Write-Host "Executing: $Nefcon --remove-device-node --hardware-id $HardwareId --class-guid $ClassGuid"
& $Nefcon --remove-device-node --hardware-id $HardwareId --class-guid $ClassGuid 2>&1 | ForEach-Object { Write-Host "  $_" }
if ($LASTEXITCODE -ne 0) {
    # A missing node (already removed) also reports an error; do not fail the uninstall.
    Write-Warning "nefconc.exe exited with code $LASTEXITCODE (device node may already be removed)"
}
exit 0
