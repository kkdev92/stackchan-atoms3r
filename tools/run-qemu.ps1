#Requires -Version 5.1
<#
.SYNOPSIS
    Run the firmware under QEMU, with no board attached.

.DESCRIPTION
    Builds, merges a flash image, and boots it in QEMU. The device output is
    shown in the terminal.

    The wrapper builds and merges on Windows, then runs tools/run-qemu.sh
    inside WSL.

    Run with -Install once before the first use.

.EXAMPLE
    .\tools\run-qemu.ps1
    .\tools\run-qemu.ps1 -Install          # first time; puts QEMU into WSL
    .\tools\run-qemu.ps1 -Check            # fail if the boot did not finish
    .\tools\run-qemu.ps1 -Seconds 30
    .\tools\run-qemu.ps1 -WaitForGdb       # start halted, wait for a debugger
#>
[CmdletBinding()]
param(
    # How long to let it run before stopping it.
    [int]$Seconds = 20,

    # Fetch QEMU and its dependencies into WSL. Needed once.
    [switch]$Install,

    # Check the startup log and exit non-zero if the boot did not finish.
    [switch]$Check,

    # Boot with the CPU halted and wait for a debugger on localhost:1234.
    #
    # Named this way because -Debug is already taken: CmdletBinding gives every
    # script a common parameter by that name.
    [switch]$WaitForGdb,

    # Use whatever was built last instead of building again.
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

Set-Location (Split-Path $PSScriptRoot -Parent)

function Test-Wsl {
    if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) {
        Write-Host ''
        Write-Host 'WSL was not found.' -ForegroundColor Red
        Write-Host '  QEMU runs inside WSL. Install it with: wsl --install -d Ubuntu'
        exit 1
    }
    # wsl writes UTF-16LE unless told otherwise. WSL_UTF8 switches it to UTF-8;
    # the NUL strip covers older versions that ignore the variable.
    $env:WSL_UTF8 = '1'
    $distros = (& wsl --list --quiet) -replace "`0", ''

    # Written as -not (... -match ...) rather than -notmatch: against an array,
    # -match filters and returns the elements that matched, so -notmatch would
    # return the ones that did not, never a true or false.
    if (-not ($distros -match 'Ubuntu')) {
        Write-Host ''
        Write-Host 'WSL has no Ubuntu distribution.' -ForegroundColor Red
        Write-Host '  Install one with: wsl --install -d Ubuntu'
        exit 1
    }
}

function ConvertTo-WslPath([string]$WindowsPath) {
    $full = (Resolve-Path $WindowsPath).Path
    '/mnt/' + $full.Substring(0, 1).ToLower() + ($full.Substring(2) -replace '\\', '/')
}

Test-Wsl

$repo = ConvertTo-WslPath '.'
$script = "$repo/tools/run-qemu.sh"

if ($Install) {
    & wsl -d Ubuntu -e bash -lc "bash '$script' --install-only"
    if ($LASTEXITCODE -ne 0) {
        Write-Host '  installing QEMU failed.' -ForegroundColor Red
        exit 1
    }
}

$pioEnv = 'atoms3r-qemu'
$buildDir = ".pio/build/$pioEnv"

if (-not $SkipBuild) {
    Write-Host ''
    Write-Host "=== building ($pioEnv) ===" -ForegroundColor Cyan
    . "$PSScriptRoot\find-platformio.ps1"
    $pio = Get-PlatformIOPath
    & $pio run -e $pioEnv
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host ''
Write-Host '=== merging the flash image ===' -ForegroundColor Cyan

# QEMU starts from the ROM bootloader, exactly as the chip does, so it needs
# the whole flash rather than the application alone. Handed just the
# application it would find no second-stage bootloader and no partition table,
# and would never reach any of this project's code.
#
# Merged here rather than in WSL because esptool and its dependencies come with
# PlatformIO, which is installed on the Windows side.
. "$PSScriptRoot\find-platformio.ps1"
$pio = Get-PlatformIOPath
$esptool = Join-Path $HOME '.platformio\packages\tool-esptoolpy\esptool.py'
$python = Join-Path (Split-Path $pio -Parent) 'python.exe'
$image = "$buildDir/flash_image.bin"

& $python $esptool --chip esp32s3 merge_bin --fill-flash-size 8MB -o $image `
    0x0      "$buildDir/bootloader.bin" `
    0x8000   "$buildDir/partitions.bin" `
    0x10000  "$buildDir/firmware.bin"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$arguments = @('--image', (ConvertTo-WslPath $image), '--seconds', $Seconds)
if ($Check)      { $arguments += '--check' }
if ($WaitForGdb) { $arguments += '--wait-for-gdb' }

& wsl -d Ubuntu -e bash -lc "bash '$script' $($arguments -join ' ')"
exit $LASTEXITCODE
