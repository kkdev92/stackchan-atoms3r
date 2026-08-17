#Requires -Version 5.1
<#
.SYNOPSIS
    Run the verification gate locally, before pushing.

.DESCRIPTION
    Runs the repository's local invariant, test, analysis, build, and emulator
    checks. Optional switches select the slower firmware checks.

    On Linux and macOS, run the corresponding commands directly:

        python tools/check-invariants.py
        pio test  -e native
        pio check -e native
        python tools/run-clang-tidy.py
        tools/run-qemu.sh --check

.EXAMPLE
    .\tools\check.ps1              # invariants, tests, static analysis
    .\tools\check.ps1 -Build       # ...and build all four configurations
    .\tools\check.ps1 -Boot        # ...and boot the emulator
    .\tools\check.ps1 -All         # all available checks
#>
[CmdletBinding()]
param(
    # Build all four firmware configurations. This resource-intensive step is
    # optional for the default local run.
    [switch]$Build,

    # Boot the emulator and check the startup log.
    [switch]$Boot,

    # Everything.
    [switch]$All
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

if ($All) { $Build = $true; $Boot = $true }

# Select the x86-64 toolchain packages used through emulation on Windows on Arm
# and natively on an x86-64 host.
if (-not $env:PLATFORMIO_SYSTEM_TYPE) { $env:PLATFORMIO_SYSTEM_TYPE = 'windows_amd64' }

. "$PSScriptRoot\find-platformio.ps1"
$pio = Get-PlatformIOPath

$results = [System.Collections.Generic.List[object]]::new()

function Invoke-Lane {
    param([string]$Name, [scriptblock]$Body)

    Write-Host ''
    Write-Host "=== $Name ===" -ForegroundColor Cyan
    $started = Get-Date
    & $Body
    $ok = $LASTEXITCODE -eq 0
    $results.Add([pscustomobject]@{
        Lane     = $Name
        Result   = if ($ok) { 'passed' } else { 'FAILED' }
        Duration = '{0:mm\:ss}' -f (New-TimeSpan -Start $started -End (Get-Date))
    })
}

# Run the fast invariant check first for an actionable boundary error.
Invoke-Lane 'invariants' { python tools/check-invariants.py }

Invoke-Lane 'host tests' { & $pio test -e native }

Invoke-Lane 'cppcheck' { & $pio check -e native --fail-on-defect=medium --fail-on-defect=high }

# A local machine without clang-tidy reports this lane as skipped. CI installs
# the tool and does not pass --allow-missing.
Invoke-Lane 'clang-tidy' {
    $output = & python tools/run-clang-tidy.py --allow-missing 2>&1
    $output | ForEach-Object { Write-Host $_ }
    if ($output -match 'SKIPPED') { $script:clangTidySkipped = $true }
}
if ($clangTidySkipped) {
    ($results | Where-Object Lane -eq 'clang-tidy').Result = 'skipped'
}

if ($Build) {
    foreach ($environment in 'atoms3r-safe', 'atoms3r-debug', 'atoms3r-qemu', 'atoms3r-release') {
        Invoke-Lane "build $environment" { & $pio run -e $environment }
    }
}

if ($Boot) {
    # -Check makes it exit non-zero unless the startup log shows the self-checks
    # passing, the device reporting itself ready, and no panic. The assertions
    # live in run-qemu.sh, which is the same code CI runs.
    #
    # -SkipBuild only when this run already built that configuration.
    Invoke-Lane 'boot' {
        & "$PSScriptRoot\run-qemu.ps1" -Check -Seconds 25 -SkipBuild:$Build
    }
}

Write-Host ''
$results | Format-Table -AutoSize

if ($results.Result -contains 'FAILED') {
    Write-Host 'gate FAILED' -ForegroundColor Red
    exit 1
}
Write-Host 'gate passed' -ForegroundColor Green
