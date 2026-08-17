# Windows on Arm development setup

The firmware toolchain currently used by this repository is installed as an
x64 Windows toolchain. Windows on Arm can run it through x64 emulation.

This setup is needed only on Windows on Arm. Other hosts should follow the
normal PlatformIO installation instructions.

## 1. Install x64 Python

Download the current supported **Windows installer (64-bit)** from
<https://www.python.org/downloads/windows/>.

Install it for the current user in a dedicated directory. Keeping it separate
from an Arm64 Python installation avoids changing an existing Python setup.

In the following examples, replace the path with the x64 `python.exe` that was
installed:

```powershell
$StackchanPython = 'C:\Path\To\x64\python.exe'
$StackchanCore = Join-Path $env:LOCALAPPDATA 'PlatformIO-Core-x64'

& $StackchanPython -m venv $StackchanCore
& "$StackchanCore\Scripts\python.exe" -m pip install --upgrade pip
& "$StackchanCore\Scripts\python.exe" -m pip install platformio==6.1.19
& "$StackchanCore\Scripts\pio.exe" --version
```

Do not place this environment inside `~/.platformio/penv`; that directory may
be managed and recreated by the editor extension.

## 2. Select x64 PlatformIO packages

Set the PlatformIO system type for the current user:

```powershell
[Environment]::SetEnvironmentVariable(
  'PLATFORMIO_SYSTEM_TYPE',
  'windows_amd64',
  'User'
)
```

Open a new terminal after setting it. Confirm:

```powershell
$env:PLATFORMIO_SYSTEM_TYPE
& "$env:LOCALAPPDATA\PlatformIO-Core-x64\Scripts\pio.exe" system info
```

The variable belongs to the Windows on Arm host configuration and is not set
by `platformio.ini`.

## 3. Put the dedicated Core on PATH

Add this directory to the user PATH:

```text
%LOCALAPPDATA%\PlatformIO-Core-x64\Scripts
```

Open a new terminal and check:

```powershell
pio --version
```

If the editor extension is used, point it to the same directory in user
settings:

```jsonc
"platformio-ide.useBuiltinPIOCore": false,
"platformio-ide.useBuiltinPython": false,
"platformio-ide.customPATH": "C:\\Users\\<user>\\AppData\\Local\\PlatformIO-Core-x64\\Scripts"
```

Use an absolute path for `customPATH`, then restart the editor.

## 4. Install a host C++ toolchain

Native tests and clang-tidy require host tools. Install the current x86-64
MSYS2 distribution from <https://www.msys2.org/>.

Open the **MSYS2 UCRT64** shell and perform a complete update:

```bash
pacman -Suy
```

If the shell asks to close, reopen UCRT64 and run the update again:

```bash
pacman -Suy
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-clang-tools-extra
```

MSYS2 does not support partial upgrades. Follow its current update guidance at
<https://www.msys2.org/docs/updating/>.

Add the UCRT64 binary directory to the user PATH, normally:

```text
C:\msys64\ucrt64\bin
```

Confirm from a new PowerShell session:

```powershell
g++ --version
clang-tidy --version
```

## 5. Install WSL for the emulator check

The repository's Windows emulator wrapper uses Ubuntu under WSL:

```powershell
wsl --install -d Ubuntu
```

Restart Windows if requested, finish the Ubuntu first-run setup, and then run:

```powershell
.\tools\run-qemu.ps1 -Install
.\tools\run-qemu.ps1 -Check
```

The first command installs the emulator dependencies inside the Ubuntu
distribution. Review `tools/run-qemu.sh` if you need to audit the packages
before installation.

## 6. Verify the repository

From the repository root:

```powershell
python tools/check-invariants.py
pio test -e native
pio check -e native
python tools/run-clang-tidy.py
pio run -e atoms3r-safe
.\tools\run-qemu.ps1 -Check
```

Or use the helper:

```powershell
.\tools\check.ps1 -All
```

If `pio` resolves to another installation, run:

```powershell
Get-Command pio
where.exe pio
```

The expected path is the dedicated `PlatformIO-Core-x64\Scripts` directory.

## Troubleshooting

### Package reported unavailable for `windows_arm64`

Check that a new terminal sees:

```powershell
$env:PLATFORMIO_SYSTEM_TYPE
```

It should print `windows_amd64`.

### Python package tries to build from source

Confirm that the PlatformIO Core interpreter is the installed x64 Python
environment, not an Arm64 editor-managed environment.

### Editor and terminal use different PlatformIO installations

Compare `pio --version` in the terminal with the extension log. Recheck
`platformio-ide.customPATH` and restart the editor.

### QEMU check cannot find Ubuntu

Run `wsl --list --verbose` and confirm a distribution whose name contains
`Ubuntu` is installed.
