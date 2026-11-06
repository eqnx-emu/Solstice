<#
.SYNOPSIS
Removes the SudoVDA device node and certificate installed by Solstice (idempotent).

.DESCRIPTION
Removes only what install.ps1 recorded in HKLM\SOFTWARE\Solstice\SudoVDA, because other products (for example
Apollo) install the same driver, certificate and hardware ID:
- The recorded device node (InstanceId) is removed with pnputil /remove-device. Without a record, every SudoVDA
  device node is left alone. Exits non-zero when the recorded node could not be removed.
- The certificate is removed from a store only when install.ps1 recorded adding it there (CertAddedRoot,
  CertAddedTrustedPublisher), our node is gone (or there was none), and the enumeration succeeded and found no
  SudoVDA device node left. Otherwise it is kept, and the reason and the manual certutil command are logged.
  Failing to remove the certificate is only a warning.
The registry key is deleted once nothing recorded in it is left.
Paths are resolved relative to this script.
#>
$ErrorActionPreference = 'Stop'

$HardwareId = 'root\sudomaker\sudovda'
$Cert = Join-Path $PSScriptRoot 'sudovda.cer'
$Certutil = Join-Path $env:SystemRoot 'System32\certutil.exe'
$Pnputil = Join-Path $env:SystemRoot 'System32\pnputil.exe'
$StateKey = 'HKLM:\SOFTWARE\Solstice\SudoVDA'

# List the instance IDs of the existing SudoVDA device nodes.
# Returns an array (possibly empty), or $null when the devices could not be enumerated.
# The arrays are returned with the unary comma so that PowerShell does not unroll them.
# pnputil output is localised, so instance IDs are matched by their shape (ROOT\<enumerator>\<number>) instead of by label.
function Get-SudoVdaInstanceIds {
    try {
        $lines = & $Pnputil /enum-devices /deviceid $HardwareId 2>&1
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

# Read one value recorded by install.ps1, or $null when there is none.
function Get-StateValue {
    param([string]$Name)
    try {
        return (Get-ItemProperty -LiteralPath $StateKey -Name $Name -ErrorAction Stop).$Name
    } catch {
        return $null
    }
}

# Forget one recorded value.
function Remove-StateValue {
    param([string]$Name)
    if (Test-Path -LiteralPath $StateKey) {
        Remove-ItemProperty -LiteralPath $StateKey -Name $Name -ErrorAction SilentlyContinue
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

# Remove only the device node install.ps1 created; nodes of other products share the hardware ID.
$ownId = Get-StateValue 'InstanceId'
$ownNodeGone = $true
if ($ownId) {
    Write-Host "Executing: $Pnputil /remove-device $ownId"
    $output = & $Pnputil /remove-device $ownId 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host "  $_" }
    if ($code -eq 0) {
        Write-Host "SudoVDA device node $ownId removed"
    } else {
        # pnputil also fails when the node no longer exists, which is not an error.
        $listed = Get-SudoVdaInstanceIds
        if ($null -ne $listed -and ($listed -notcontains "$ownId".ToUpperInvariant())) {
            Write-Host "SudoVDA device node $ownId no longer exists, nothing to remove"
        } else {
            Write-Warning "The SudoVDA device node $ownId could not be removed (pnputil exit code $code)"
            $ownNodeGone = $false
            $failed = $true
        }
    }
    if ($ownNodeGone) {
        Remove-StateValue 'InstanceId'
    }
} else {
    Write-Host 'No SudoVDA device node was recorded as installed by Solstice, leaving all SudoVDA device nodes in place'
}

# The certificate may be removed only when no SudoVDA node, ours or another product's, still needs it.
$remaining = Get-SudoVdaInstanceIds
$thumbprint = $null
try {
    if (-not (Test-Path -LiteralPath $Cert)) {
        throw "sudovda.cer not found at $Cert"
    }
    $thumbprint = (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $Cert).Thumbprint
} catch {
    Write-Warning "Could not read the SudoVDA certificate: $($_.Exception.Message)"
}

foreach ($store in @('Root', 'TrustedPublisher')) {
    $name = "CertAdded$store"
    if ((Get-StateValue $name) -ne 1) {
        Write-Host "The SudoVDA certificate in the $store store was not added by Solstice, keeping it"
        Remove-StateValue $name
        continue
    }
    $reason = $null
    if ($null -eq $thumbprint) {
        $reason = 'its thumbprint could not be read from sudovda.cer'
    } elseif (-not $ownNodeGone) {
        $reason = 'the Solstice SudoVDA device node could not be removed'
    } elseif ($null -eq $remaining) {
        $reason = 'the SudoVDA device nodes could not be listed'
    } elseif ($remaining.Count -gt 0) {
        $reason = "SudoVDA device nodes of other software remain ($($remaining -join ', '))"
    }
    if ($null -ne $reason) {
        $manual = if ($null -eq $thumbprint) { "$Certutil -delstore $store <thumbprint of sudovda.cer>" } else { "$Certutil -delstore $store $thumbprint" }
        Write-Warning "Keeping the SudoVDA certificate in the $store store because $reason. To remove it manually, run from an elevated prompt: $manual"
        continue
    }
    if (Remove-SudoVdaCertificate -Store $store -Thumbprint $thumbprint) {
        Remove-StateValue $name
    } else {
        Write-Warning "To remove it manually, run from an elevated prompt: $Certutil -delstore $store $thumbprint"
    }
}

# Delete the record once nothing recorded in it is left.
if (Test-Path -LiteralPath $StateKey) {
    $left = @((Get-Item -LiteralPath $StateKey).Property)
    if ($left.Count -eq 0) {
        Remove-Item -LiteralPath $StateKey -Force -ErrorAction SilentlyContinue
        $parent = Split-Path -Parent $StateKey
        if ((Test-Path -LiteralPath $parent) -and @((Get-ChildItem -LiteralPath $parent)).Count -eq 0 -and @((Get-Item -LiteralPath $parent).Property).Count -eq 0) {
            Remove-Item -LiteralPath $parent -Force -ErrorAction SilentlyContinue
        }
    } else {
        Write-Host "Keeping $StateKey, still recording: $($left -join ', ')"
    }
}

if ($failed) {
    exit 1
}
exit 0
