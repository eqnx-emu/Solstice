<#
.SYNOPSIS
Removes the SudoVDA virtual display device node and its certificate (idempotent).

.DESCRIPTION
Removes the device node, then checks that no SudoVDA device node is left. A node that does not exist is not an
error. Exits non-zero when a node could not be removed (or the removal failed and the nodes could not be listed).
As the last step, the driver certificate trusted by install.ps1 is removed from the Root and TrustedPublisher
stores, located by the thumbprint of sudovda.cer. Failing to remove the certificate is only a warning.
Paths are resolved relative to this script.
#>
$ErrorActionPreference = 'Stop'

$HardwareId = 'root\sudomaker\sudovda'
$ClassGuid = '4D36E968-E325-11CE-BFC1-08002BE10318'
$Nefcon = Join-Path $PSScriptRoot 'nefconc.exe'
$Cert = Join-Path $PSScriptRoot 'sudovda.cer'
$Certutil = Join-Path $env:SystemRoot 'System32\certutil.exe'

# List the instance IDs of the existing SudoVDA device nodes.
# Returns an array (possibly empty), or $null when the devices could not be enumerated.
# The arrays are returned with the unary comma so that PowerShell does not unroll them.
# pnputil output is localised, so instance IDs are matched by their shape (ROOT\<enumerator>\<number>) instead of by label.
function Get-SudoVdaInstanceIds {
    $pnputil = Join-Path $env:SystemRoot 'System32\pnputil.exe'
    try {
        $lines = & $pnputil /enum-devices /deviceid $HardwareId 2>&1
        if ($LASTEXITCODE -eq 0) {
            $ids = @($lines | ForEach-Object { "$_" } |
                    Select-String -Pattern '(?i)\bROOT\\[^\\\s]+\\\d+' -AllMatches |
                    ForEach-Object { $_.Matches } | ForEach-Object { $_.Value.ToUpperInvariant() } |
                    Sort-Object -Unique)
            return ,$ids
        }
    } catch {
        Write-Warning "pnputil could not enumerate devices: $($_.Exception.Message)"
    }
    try {
        $devices = @(Get-PnpDevice -ErrorAction Stop | Where-Object { $_.HardwareID -contains $HardwareId })
        return ,@($devices | ForEach-Object { "$($_.InstanceId)".ToUpperInvariant() } | Sort-Object -Unique)
    } catch {
        Write-Warning "Could not enumerate SudoVDA device nodes: $($_.Exception.Message)"
        return $null
    }
}

# Remove the SudoVDA certificate from one certificate store of the local machine.
# Returns $true when the certificate is not in the store (anymore).
function Remove-SudoVdaCertificate {
    param([string]$Store, [string]$Thumbprint)
    $path = "Cert:\LocalMachine\$Store\$Thumbprint"
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "The SudoVDA certificate is not in the $Store store"
        return $true
    }
    Write-Host "Executing: $Certutil -delstore $Store $Thumbprint"
    $output = & $Certutil -delstore $Store $Thumbprint 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host "  $_" }
    if ($code -ne 0 -or (Test-Path -LiteralPath $path)) {
        Write-Warning "Could not remove the SudoVDA certificate from the $Store store (certutil exit code $code)"
        return $false
    }
    return $true
}

# Native stderr must not become a terminating error under Windows PowerShell 5.1.
$ErrorActionPreference = 'Continue'
$failed = $false

$code = $null
if (Test-Path -LiteralPath $Nefcon) {
    Write-Host "Executing: $Nefcon --remove-device-node --hardware-id $HardwareId --class-guid $ClassGuid"
    $output = & $Nefcon --remove-device-node --hardware-id $HardwareId --class-guid $ClassGuid 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Warning "nefconc.exe not found at $Nefcon, cannot remove the device node"
}

# nefconc also fails when there is no node to remove, so the outcome is judged by the nodes that are left.
$remaining = Get-SudoVdaInstanceIds
if ($null -eq $remaining) {
    if ($code -ne 0) {
        Write-Warning "The SudoVDA device node could not be removed (nefconc exit code $code) and the remaining nodes could not be listed"
        $failed = $true
    }
} elseif ($remaining.Count -gt 0) {
    Write-Warning "SudoVDA device nodes are still present: $($remaining -join ', ')"
    $failed = $true
} elseif ($code -ne 0) {
    Write-Host 'No SudoVDA device node exists, nothing to remove'
} else {
    Write-Host 'SudoVDA device node removed'
}

# Last step: stop trusting the driver certificate. A failure here does not fail the uninstall.
try {
    if (-not (Test-Path -LiteralPath $Cert)) {
        throw "sudovda.cer not found at $Cert"
    }
    $thumbprint = (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $Cert).Thumbprint
    foreach ($store in @('Root', 'TrustedPublisher')) {
        $null = Remove-SudoVdaCertificate -Store $store -Thumbprint $thumbprint
    }
} catch {
    Write-Warning "Could not remove the SudoVDA certificate: $($_.Exception.Message)"
}

if ($failed) {
    exit 1
}
exit 0
