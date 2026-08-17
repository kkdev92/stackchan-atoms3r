# stackchan-atoms3r

[![CI](https://github.com/kkdev92/stackchan-atoms3r/actions/workflows/ci.yml/badge.svg)](https://github.com/kkdev92/stackchan-atoms3r/actions/workflows/ci.yml)
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/kkdev92/stackchan-atoms3r/badge)](https://scorecard.dev/viewer/?uri=github.com/kkdev92/stackchan-atoms3r)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/14114/badge)](https://www.bestpractices.dev/projects/14114)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-6.0.1-red.svg)](platformio.ini)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-6.1.19-orange.svg)](platformio.ini)
[![Board](https://img.shields.io/badge/board-M5Stack%20AtomS3R-blue.svg)](docs/hardware/pins-and-peripherals.md)

Bring a small Stack-chan to life with an animated face, button-controlled
conversations, and a local HTTP API.
The firmware sends audio only when a conversation is started and contains no
telemetry service.
*Built as a compact, inspectable base for one documented hardware setup.*

> **Status:** 0.1.0 (best-effort maintenance)

---

## Table of Contents

- [Features](#features)
- [Hardware and Tooling](#hardware-and-tooling)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [Why stackchan-atoms3r](#why-stackchan-atoms3r)
- [Usage](#usage)
- [Configuration](#configuration)
- [Known Limitations](#known-limitations)
- [How It Works](#how-it-works)
- [Security and Privacy](#security-and-privacy)
- [Build and Verification](#build-and-verification)
- [Troubleshooting](#troubleshooting)
- [Documentation](#documentation)
- [Changelog](#changelog)
- [Contributing](#contributing)
- [Support & Maintenance Policy](#support--maintenance-policy)
- [License](#license)
- [Acknowledgments](#acknowledgments)

---

## Features

- **Animated Face**: Six expressions — `neutral`, `happy`, `sad`, `doubt`, `sleepy`, and `angry`
- **Button Controls**: Start or cancel a conversation and show device information without another controller
- **Voice-Base Audio**: Three-second, 16 kHz mono recording and reply playback
- **Local Setup**: Configure Wi-Fi through a WPA2 setup access point
- **Authenticated API**: Inspect state and control the device through one token-protected command endpoint
- **Configurable Gateway**: Send recorded audio or supplied text and receive an SSE reply stream
- **Diagnostic Gateway**: Check the complete device-side conversation path with generated tones
- **Host Verification**: Hardware-independent tests plus a QEMU startup check

---

## Hardware and Tooling

| Item | Requirement |
| --- | --- |
| Board | M5Stack AtomS3R |
| Audio | M5Stack Atomic Voice Base |
| Connection | A data-capable USB cable |
| Build | PlatformIO Core 6.1.19 |
| Host language | Python 3 |
| Project source baseline | C++17 |

Dependency versions are pinned in [`platformio.ini`](platformio.ini) and the
[display component manifest](src/platform/stackchan_display/idf_component.yml).
This repository does not distribute prebuilt firmware.

---

## Installation

### Build from Source

```bash
git clone https://github.com/kkdev92/stackchan-atoms3r.git
cd stackchan-atoms3r
pio test -e native
pio run -e atoms3r-safe
```

`atoms3r-safe` is the recommended environment for the first flash. It keeps
startup logs readable and does not configure one-way security fuses.

### Flash the Device

```bash
python tools/flash.py
```

The script builds the safe configuration, flashes it, captures the startup
log, and checks that startup completed. If more than one serial device is
connected, select the port explicitly:

```bash
python tools/flash.py --port COM7
```

---

## Quick Start

1. Build and flash the firmware using the steps above
2. Connect to the `STACKCHAN-...` setup network shown on the display and [configure Wi-Fi](#wi-fi)
3. Triple-press the device button to show its IP address
4. Hold the button for about half a second while idle to print the API token to the serial log
5. Send `device.describe` to confirm the commands available on that unit

```bash
curl -X POST http://DEVICE_IP/api/v1/command \
  -H 'Content-Type: application/json' \
  -H 'X-StackChan-Token: YOUR_TOKEN' \
  -d '{"v":1,"kind":"command","id":"1","name":"device.describe","payload":{}}'
```

The token remains valid across restarts until it is rotated or NVS is erased.
Keep it out of issue reports and shared logs.

---

## Why stackchan-atoms3r

Display, audio, networking, and a remote conversation stream all meet inside
one small device. When those responsibilities are mixed together, it becomes
hard to tell which behavior is implemented, which hardware owns a pin, and
what can still respond while a conversation is running.

This repository keeps that boundary visible. The hardware profile is explicit,
the command surface is small, buffers and timeouts are bounded, and the
hardware-independent decisions run in host tests. The result is a practical
baseline that can be read from startup through to playback without relying on
undocumented behavior.

The repository intentionally stops at the device boundary. It includes a
diagnostic gateway for checking the audio and streaming path, but not a
production speech or language service.

---

## Usage

### Button Controls

| Gesture | Action |
| --- | --- |
| Single press | Start a conversation |
| Double press | Show detected hardware information |
| Triple press | Show the current IP address and log device information |
| Hold while idle | Log device information and the API token |
| Hold during a conversation | Cancel the conversation |

Four or more presses have no assigned action.

### Command API

Every command uses `POST /api/v1/command` with the `X-StackChan-Token` header.
The current build registers:

| Command | Purpose |
| --- | --- |
| `device.describe` | Identity, firmware version, and registered capabilities |
| `device.state` | Conversation, network, expression, and emergency-stop state |
| `device.health` | Uptime and memory status |
| `face.set_expression` | Select one of the six expressions |
| `conversation.start` | Record and send audio, or send supplied `text` |
| `conversation.cancel` | Cancel the current conversation |
| `estop.engage` | Cancel current work and block new conversations and expression changes |
| `estop.clear` | Clear the emergency stop |
| `gateway.configure` | Store the base URL used for conversations |
| `token.rotate` | Replace the API token |

The full request, response, error, provisioning, and gateway formats are in
the [device interface reference](docs/api/device-interface.md).

### Check a Conversation

Start the diagnostic gateway on a computer reachable from the device:

```bash
python tools/echo-gateway.py
```

Configure the device with one of the addresses printed by the script:

```bash
curl -X POST http://DEVICE_IP/api/v1/command \
  -H 'Content-Type: application/json' \
  -H 'X-StackChan-Token: YOUR_TOKEN' \
  -d '{"v":1,"kind":"command","id":"2","name":"gateway.configure","payload":{"url":"http://GATEWAY_IP:8080"}}'
```

Press the device button once. It records for three seconds, posts the WAV to
`/v1/converse`, and plays two generated tones while changing expression.
This checks the device-side path; it does not perform speech recognition, text
generation, or speech synthesis.

---

## Configuration

Wi-Fi credentials, the API token, and the gateway URL are stored in NVS.

### Wi-Fi

When no credentials are stored, the device creates a WPA2 setup access point
named `STACKCHAN-...`. Its password is shown on the display. Connect a computer
to that network; the setup API is normally available at `http://192.168.4.1`.

List the networks found before the setup access point started:

```bash
curl http://192.168.4.1/scan
```

Windows PowerShell:

```powershell
curl.exe -X POST http://192.168.4.1/save `
  -H "Content-Type: application/json" `
  -d '{"ssid":"YOUR_SSID","pass":"YOUR_PASSWORD"}'
```

Bash:

```bash
curl -X POST http://192.168.4.1/save \
  -H 'Content-Type: application/json' \
  -d '{"ssid":"YOUR_SSID","pass":"YOUR_PASSWORD"}'
```

The device stores the credentials and starts connecting asynchronously. The
setup routes (`/`, `/scan`, `/save`, and `/info`) accept requests only through
the setup access point. Once started, that access point remains available until
the device restarts. On a later boot with usable stored credentials, it stays
off unless repeated connection failures open it for recovery.

### API Token

Open a serial monitor, then hold the button for about half a second while the
device is idle:

```bash
pio device monitor
```

The log prints `access token: ...`. Send that value in the
`X-StackChan-Token` header. `token.rotate` replaces it; erasing NVS removes it
along with the Wi-Fi credentials and gateway URL.

### Gateway

`gateway.configure` accepts an `http://` base URL from 8 to 120 bytes. The
firmware appends `/v1/converse` when a conversation starts. A successful reply
is an SSE stream containing version 1 event envelopes. Do not put credentials
or other secrets in the URL because it is stored and written to device logs.

---

## Known Limitations

- **Documented hardware only.** The current implementation supports the AtomS3R and voice-base configuration described here
- **Fixed recording length.** Each recorded conversation captures three seconds of audio
- **Half-duplex audio.** Recording and playback do not run at the same time
- **API-based setup.** The setup service is a small HTTP API, not a browser configuration form
- **Local plain HTTP.** The command server and gateway protocol do not provide transport encryption
- **Bounded gateway URL.** Only `http://` URLs of at most 120 bytes are accepted
- **Partial emulation.** QEMU checks startup but does not emulate Wi-Fi, the display, I2C, or audio
- **Physical verification required.** Hardware-dependent behavior still needs a real device
- **Dependency warning.** The pinned display component emits legacy-I2C and LEDC deprecation warnings with ESP-IDF 6.0.1
- **No additional device features.** Servo, infrared, OTA update handling, wake-word detection, and voice activity detection are not implemented
- **Coverage.** This version has not been soak-tested across multiple boards and networks

---

## How It Works

The source is split by responsibility:

```text
src/core/       protocol, state, parsers, and hardware-independent decisions
src/platform/   device, network, audio, display, and transport adapters
src/main/       component construction and dependency wiring
```

The native test environment builds only `src/core`, keeping platform headers
out of the hardware-independent layer.

A recorded conversation follows this path:

1. A button press or authenticated command starts the conversation task
2. The device captures three seconds of 16 kHz, signed 16-bit mono PCM
3. A WAV request is sent to the configured gateway at `/v1/converse`
4. The firmware reads bounded SSE events while the command server stays available
5. Reply audio is staged by sentence, paired with an expression, and played
6. Cancellation or emergency stop closes the active gateway request

See the [architecture overview](docs/architecture/overview.md)
for the current component graph and runtime flow.

---

## Security and Privacy

- **Token-Protected Commands**: The command API requires a per-device token
- **User-Initiated Recording**: Audio is sent only after a button press or an authenticated `conversation.start` command
- **No Telemetry**: The firmware contains no telemetry service
- **Local Storage**: Wi-Fi credentials, the API token, and the gateway URL are stored in NVS
- **Plain HTTP**: Device and gateway traffic is visible to other systems able to observe the local network
- **Physical Access Matters**: Serial access and flash access can expose device configuration

Use the device only on a network you control. Do not expose its HTTP server or
the diagnostic gateway directly to the internet. For the reporting process and
the full operational guidance, see [SECURITY.md](SECURITY.md).

---

## Build and Verification

| Environment | Purpose |
| --- | --- |
| `native` | Host tests and cppcheck for `src/core` |
| `native-sanitize` | The host tests under AddressSanitizer and UndefinedBehaviorSanitizer |
| `native-coverage` | The host tests instrumented, for the coverage measurement below |
| `atoms3r-safe` | First flash and hardware bring-up |
| `atoms3r-debug` | Hardware development with debug logging |
| `atoms3r-qemu` | QEMU boot check with Wi-Fi disabled |
| `atoms3r-release` | Release candidate with the OTA partition layout |

Useful checks:

```bash
python tools/check-invariants.py
pio test -e native
pio check -e native
python tools/run-clang-tidy.py
python tools/check-coverage.py   # needs gcovr
pio test -e native-sanitize      # Linux; MinGW ships no libasan
tools/run-qemu.sh --check
```

`clang-tidy` and `gcovr` are installed separately. On Windows, `./tools/check.ps1`
runs the repository checks using the tools available on that machine, and reports
a check as skipped rather than passed when its tool is missing. The same checks
run in CI on every push and pull request to `main`.

Because `src/core` contains no ESP-IDF, the entire decision layer runs on a PC,
and its test coverage is measured rather than assumed: 93.2% of lines, 82.6% of
branches, and 99.7% of functions. CI enforces a floor of 90% line and 80% branch
coverage, and runs the same tests under AddressSanitizer and
UndefinedBehaviorSanitizer on every pull request.

Before the first QEMU run, install it with `tools/run-qemu.sh --install-only`
on Linux or `.\tools\run-qemu.ps1 -Install` on Windows.

---

## Troubleshooting

- **`pio` is not found**: Install PlatformIO Core 6.1.19 and make sure its executable is on `PATH`. Windows on Arm users can follow the [dedicated setup guide](docs/operations/windows-on-arm-setup.md)
- **The setup network does not appear**: Check the serial log. A unit with stored credentials tries them first and raises the setup access point after repeated failures
- **Start over with network setup**: Run the erase command below, then flash again. This also removes the API token and gateway URL

  ```bash
  pio run -e atoms3r-safe -t erase
  python tools/flash.py
  ```

- **The API rejects the token**: Retrieve the current token with an idle button hold and check the `X-StackChan-Token` header
- **Audio commands are unavailable**: Check that the voice base is seated correctly, then reboot and inspect the hardware-detection log
- **A conversation produces no reply**: Confirm that the gateway URL is configured and reachable from the device. `tools/echo-gateway.py` provides a small local check
- **The QEMU wrapper cannot find Ubuntu**: Install an Ubuntu WSL distribution or use the documented direct Linux script

---

## Documentation

| Document | What is in it |
| --- | --- |
| [Device interface](docs/api/device-interface.md) | Routes, commands, errors, and the gateway request and response contract |
| [Firmware architecture](docs/architecture/overview.md) | Source layout, startup order, the tasks, and how a conversation flows |
| [Design principles](docs/architecture/design-principles.md) | The boundaries the layout exists to hold, and what enforces each |
| [Pins and peripherals](docs/hardware/pins-and-peripherals.md) | What is wired where, and the register values that were measured |
| [Windows on Arm setup](docs/operations/windows-on-arm-setup.md) | Windows on Arm toolchain setup |
| [Contributing guide](CONTRIBUTING.md) | How to run the checks, and what a change is expected to bring |
| [Security policy](SECURITY.md) | How to report something privately |

---

## Changelog

Notable changes are in [CHANGELOG.md](CHANGELOG.md); individual changes are in the
pull requests.

To find out what a device is running, ask it. `device.describe` reports the
firmware version and the ELF SHA-256 of the image it booted. Builds embed no
timestamp, so that hash identifies the source the image was built from — a local
modification changes it.

---

## Contributing

Bug reports, documentation fixes, and focused pull requests are welcome —
thank you for helping make stackchan-atoms3r better 🙌
Please see [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow.

If a check cannot be run in your environment, note that in the pull request so
it can be completed during review. Opening an issue before a larger change is
appreciated because it helps confirm the current scope and hardware needs.

---

## Support & Maintenance Policy

stackchan-atoms3r is a personal hobby project maintained in spare time. Support
is best-effort, so issue responses and releases may take a little time — thank
you for your patience.

Helpful details for a bug report include:

- Board and voice-base revision, if known
- Build environment and PlatformIO version
- The relevant serial log with tokens, passwords, and private addresses removed
- The command request and response, if the issue involves the API
- Whether the behavior also appears with the diagnostic gateway

Security-related reports should follow [SECURITY.md](SECURITY.md).
Really appreciate you trying stackchan-atoms3r 💛

---

## License

Original code in this repository is licensed under the [MIT License](LICENSE).
A firmware image also contains third-party components; keep [NOTICE](NOTICE)
and the applicable files in [`licenses/`](licenses/) with redistributed
binaries.

---

## Acknowledgments

This project builds on the Stack-chan concept and on third-party hardware,
framework, and display work. Names are used only where needed to describe
compatibility, dependencies, and attribution. Copyright and license notices
are collected in [NOTICE](NOTICE).

This firmware was inspired by
[A-Uta/StackChan_Minimal](https://github.com/A-Uta/StackChan_Minimal), an
earlier project for the same hardware. No source files from that repository
are included here; this is an independent ESP-IDF implementation.
