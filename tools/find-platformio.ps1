#Requires -Version 5.1
<#
.SYNOPSIS
    Locate the PlatformIO Core executable.

.DESCRIPTION
    PlatformIO is usually installed by the PlatformIO IDE extension for VS
    Code, which puts Core under %USERPROFILE%\.platformio\penv and adds it to
    PATH only inside VS Code's integrated terminal. Run a script from an
    ordinary PowerShell window and a bare `pio` is not found.

    So the scripts here resolve it rather than assuming it, and behave the
    same from either terminal.

    Dot-source it:

        . "$PSScriptRoot\find-platformio.ps1"
        $pio = Get-PlatformIOPath
        & $pio run -e atoms3r-safe
#>

function Get-PlatformIOPath {
    [CmdletBinding()]
    param()

    # Whatever is on PATH wins.
    #
    # This ordering matters on Windows on Arm. The extension may rebuild its
    # environment with an ARM64 Python, and that Core cannot install one of
    # esptool's dependencies, so builds fail there. The workaround is an
    # x86-64 Core installed elsewhere and put on PATH ahead of it. See
    # docs/operations/windows-on-arm-setup.md.
    $onPath = Get-Command pio -CommandType Application -ErrorAction SilentlyContinue
    if ($onPath) {
        return $onPath.Source
    }

    # Otherwise use the one the extension installed, which is the normal case.
    $bundled = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
    if (Test-Path $bundled) {
        return $bundled
    }

    Write-Host ''
    Write-Host 'PlatformIO Core was not found.' -ForegroundColor Red
    Write-Host '  Installing the PlatformIO IDE extension for VS Code installs Core with it.'
    Write-Host '  Extension ID: platformio.platformio-ide'
    exit 1
}
