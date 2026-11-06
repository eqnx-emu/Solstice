<#
.SYNOPSIS
Installs the SudoVDA virtual display driver (idempotent).

.DESCRIPTION
Trusts the driver certificate, recreates the device node (unless one is still present and could not be
removed, in which case only the driver is updated) and installs the driver.
Every external command is checked. Exits non-zero on failure so the caller can log it.
Paths are resolved relative to this script.
#>
$ErrorActionPreference = 'Stop'

$HardwareId = 'root\sudomaker\sudovda'
$ClassGuid = '4D36E968-E325-11CE-BFC1-08002BE10318'
$Cert = Join-Path $PSScriptRoot 'sudovda.cer'
$Inf = Join-Path $PSScriptRoot 'SudoVDA.inf'
$Nefcon = Join-Path $PSScriptRoot 'nefconc.exe'
$Certutil = Join-Path $env:SystemRoot 'System32\certutil.exe'

# Run a native command, print its output and fail on a non-zero exit code.
# Returns $true on success, and $false when a failure was ignored through -AllowFailure.
function Invoke-Checked {
    param([string]$FilePath, [string[]]$Arguments, [switch]$AllowFailure)
    Write-Host "Executing: $FilePath $($Arguments -join ' ')"
    # Windows PowerShell 5.1 turns native stderr into a terminating error under 'Stop'.
    # Success is judged by the exit code only.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $FilePath @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    $output | ForEach-Object { Write-Host "  $_" }
    if ($code -ne 0) {
        if ($AllowFailure) {
            Write-Warning "$FilePath exited with code $code (ignored)"
            return $false
        }
        throw "$FilePath exited with code $code"
    }
    return $true
}

# List the instance IDs of the existing SudoVDA device nodes.
# Returns an array (possibly empty), or $null when the devices could not be enumerated.
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
            return $ids
        }
    } catch {
        Write-Warning "pnputil could not enumerate devices: $($_.Exception.Message)"
    }
    try {
        $devices = @(Get-PnpDevice -ErrorAction Stop | Where-Object { $_.HardwareID -contains $HardwareId })
        return @($devices | ForEach-Object { "$($_.InstanceId)".ToUpperInvariant() } | Sort-Object -Unique)
    } catch {
        Write-Warning "Could not enumerate SudoVDA device nodes: $($_.Exception.Message)"
        return $null
    }
}

foreach ($required in @($Cert, $Inf, $Nefcon, (Join-Path $PSScriptRoot 'SudoVDA.dll'), (Join-Path $PSScriptRoot 'sudovda.cat'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing required file: $required"
    }
}

# Another IddCx virtual display driver may conflict with SudoVDA. Warn only.
try {
    $others = Get-PnpDevice -Class Display -ErrorAction SilentlyContinue |
        Where-Object { $_.HardwareID -notcontains $HardwareId -and
            ($_.FriendlyName -match 'Virtual|IDD|Indirect' -or $_.HardwareID -match 'MttVDD|IddSampleDriver|VirtualDisplay') }
    foreach ($device in $others) {
        Write-Warning "Another virtual display driver is present: $($device.FriendlyName). It may conflict with SudoVDA."
    }
} catch {
    Write-Warning "Could not check for other virtual display drivers: $($_.Exception.Message)"
}

Write-Host 'Trusting the SudoVDA certificate'
$null = Invoke-Checked $Certutil @('-addstore', '-f', 'root', $Cert)
$null = Invoke-Checked $Certutil @('-addstore', '-f', 'TrustedPublisher', $Cert)

# Removing first keeps repeated installs from leaving duplicate device nodes.
# It fails when no node exists yet, which is expected on a first install, or when the device is busy.
Write-Host 'Removing any previous SudoVDA device node'
$removed = Invoke-Checked $Nefcon @('--remove-device-node', '--hardware-id', $HardwareId, '--class-guid', $ClassGuid) -AllowFailure

# Creating a node while one still exists would leave a duplicate, so check first.
$existing = Get-SudoVdaInstanceIds
if ($null -ne $existing -and $existing.Count -gt 1) {
    Write-Warning "Multiple SudoVDA device nodes exist: $($existing -join ', ')"
}
if ($null -ne $existing -and $existing.Count -ge 1) {
    if ($removed) {
        Write-Host "A SudoVDA device node is still present after removal ($($existing -join ', ')), not creating another"
    } else {
        Write-Host "The previous SudoVDA device node could not be removed ($($existing -join ', ')), not creating another and updating its driver"
    }
} else {
    Write-Host 'Creating the SudoVDA device node'
    $null = Invoke-Checked $Nefcon @('--create-device-node', '--class-name', 'Display', '--class-guid', $ClassGuid, '--hardware-id', $HardwareId)
}

Write-Host 'Installing the SudoVDA driver'
$null = Invoke-Checked $Nefcon @('--install-driver', '--inf-path', $Inf)

Write-Host 'SudoVDA driver installed'
exit 0
