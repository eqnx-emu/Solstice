<#
.SYNOPSIS
Installs the SudoVDA virtual display driver (idempotent).

.DESCRIPTION
Trusts the driver certificate, recreates the device node and installs the driver.
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
            return
        }
        throw "$FilePath exited with code $code"
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
Invoke-Checked $Certutil @('-addstore', '-f', 'root', $Cert)
Invoke-Checked $Certutil @('-addstore', '-f', 'TrustedPublisher', $Cert)

# Removing first keeps repeated installs from leaving duplicate device nodes.
# It fails when no node exists yet, which is expected on a first install.
Write-Host 'Removing any previous SudoVDA device node'
Invoke-Checked $Nefcon @('--remove-device-node', '--hardware-id', $HardwareId, '--class-guid', $ClassGuid) -AllowFailure

Write-Host 'Creating the SudoVDA device node'
Invoke-Checked $Nefcon @('--create-device-node', '--class-name', 'Display', '--class-guid', $ClassGuid, '--hardware-id', $HardwareId)

Write-Host 'Installing the SudoVDA driver'
Invoke-Checked $Nefcon @('--install-driver', '--inf-path', $Inf)

Write-Host 'SudoVDA driver installed'
exit 0
