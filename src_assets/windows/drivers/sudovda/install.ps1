<#
.SYNOPSIS
Installs the SudoVDA virtual display driver (idempotent).

.DESCRIPTION
Trusts the driver certificate, recreates the device node and installs the driver.
What Solstice itself added is recorded in HKLM\SOFTWARE\Solstice\SudoVDA so that uninstall.ps1 removes only that:
CertAddedRoot and CertAddedTrustedPublisher (DWORD) are 1 when the certificate was not in that store before this
script (or an earlier run of it) added it, and InstanceId (string) is the device node this script created.
Only the recorded node is removed before creating a new one; nodes of other products with the same hardware ID
(for example Apollo) are left alone, and no node is created while any SudoVDA node exists, so a reused node is
never recorded as ours. No node is created either when the nodes could not be enumerated and no previous node
of ours was removed; only the driver is updated then.
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
$Pnputil = Join-Path $env:SystemRoot 'System32\pnputil.exe'
$StateKey = 'HKLM:\SOFTWARE\Solstice\SudoVDA'

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
# The arrays are returned with the unary comma so that PowerShell does not unroll them: an empty array would
# otherwise become $null (indistinguishable from a failed enumeration) and a single ID a plain string.
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

# Read one value recorded by a previous install, or $null when there is none.
function Get-StateValue {
    param([string]$Name)
    try {
        return (Get-ItemProperty -LiteralPath $StateKey -Name $Name -ErrorAction Stop).$Name
    } catch {
        return $null
    }
}

# Record one value for uninstall.ps1.
function Set-StateValue {
    param([string]$Name, $Value, [string]$Type)
    if (-not (Test-Path -LiteralPath $StateKey)) {
        $null = New-Item -Path $StateKey -Force
    }
    $null = New-ItemProperty -LiteralPath $StateKey -Name $Name -Value $Value -PropertyType $Type -Force
}

# Forget one recorded value.
function Remove-StateValue {
    param([string]$Name)
    if (Test-Path -LiteralPath $StateKey) {
        Remove-ItemProperty -LiteralPath $StateKey -Name $Name -ErrorAction SilentlyContinue
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
$thumbprint = (New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList $Cert).Thumbprint
foreach ($store in @('Root', 'TrustedPublisher')) {
    # The certificate is ours to remove only when it was not in the store before we added it; an earlier claim is kept.
    $present = $null -ne (Get-ChildItem -LiteralPath "Cert:\LocalMachine\$store" | Where-Object { $_.Thumbprint -eq $thumbprint })
    $added = ((Get-StateValue "CertAdded$store") -eq 1) -or (-not $present)
    $null = Invoke-Checked $Certutil @('-addstore', '-f', $store, $Cert)
    Set-StateValue "CertAdded$store" ([int]$added) 'DWord'
    if (-not $added) {
        Write-Host "The SudoVDA certificate was already in the $store store, uninstalling Solstice will keep it there"
    }
}

# Removing our previous node first keeps repeated installs from leaving duplicate device nodes.
# Only the node recorded by an earlier install is removed: nodes of other products share the hardware ID.
$ownId = Get-StateValue 'InstanceId'
$removed = $false
if ($ownId) {
    Write-Host "Removing the previous SudoVDA device node $ownId"
    $removed = Invoke-Checked $Pnputil @('/remove-device', $ownId) -AllowFailure
} else {
    Write-Host 'No SudoVDA device node was recorded by an earlier install'
}

# Creating a node while one still exists would leave a duplicate, so check first.
# A node is created only when enumeration found none, or when enumeration failed but our previous node was removed.
$existing = Get-SudoVdaInstanceIds
if ($null -eq $existing) {
    if ($removed) {
        Write-Host 'Could not enumerate SudoVDA device nodes, but the previous node was removed'
        $create = $true
    } else {
        Write-Warning 'Could not enumerate SudoVDA device nodes and no previous node was removed; not creating another, updating the driver only'
        $create = $false
    }
} elseif ($existing.Count -ge 1) {
    if ($existing.Count -gt 1) {
        Write-Warning "Multiple SudoVDA device nodes exist: $($existing -join ', ')"
    }
    if ($ownId -and ($existing -contains "$ownId".ToUpperInvariant())) {
        Write-Host "The previous SudoVDA device node $ownId could not be removed, not creating another and updating its driver"
    } else {
        # The node belongs to another product (or was never recorded): it is used, but not claimed.
        Write-Host "A SudoVDA device node not installed by Solstice exists ($($existing -join ', ')), using it without recording it"
        Remove-StateValue 'InstanceId'
    }
    $create = $false
} else {
    Remove-StateValue 'InstanceId'
    $create = $true
}
if ($create) {
    Write-Host 'Creating the SudoVDA device node'
    $null = Invoke-Checked $Nefcon @('--create-device-node', '--class-name', 'Display', '--class-guid', $ClassGuid, '--hardware-id', $HardwareId)
    $after = Get-SudoVdaInstanceIds
    $before = if ($null -eq $existing) { @() } else { $existing }
    $new = @($after | Where-Object { $before -notcontains $_ })
    if ($null -ne $after -and $new.Count -eq 1) {
        Write-Host "Recording the SudoVDA device node $($new[0])"
        Set-StateValue 'InstanceId' $new[0] 'String'
    } else {
        Remove-StateValue 'InstanceId'
        Write-Warning 'Could not identify the created SudoVDA device node; uninstall will leave it in place'
    }
}

Write-Host 'Installing the SudoVDA driver'
$null = Invoke-Checked $Nefcon @('--install-driver', '--inf-path', $Inf)

Write-Host 'SudoVDA driver installed'
exit 0
