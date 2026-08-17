# Changelog

Notable user-visible changes, in the spirit of
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Interfaces may change
before 1.0.0, and a breaking change will be called out here. The version below is
the one the firmware reports.

**There are no release tags, and no binary is published.** A firmware image would
combine this project's code with third-party components and carry its own
verification and attribution obligations, so it is built from source instead —
which leaves a tag with no artifact to name. The history of individual changes is
the pull requests, and a device identifies its own build: `device.describe`
returns the version and the ELF SHA-256 it booted, which is stable for the same
source because the build embeds no timestamp.

## 0.1.0

The first public version, so everything is new — there is no earlier one for
anything to have changed from or been fixed against.

### Added

- Firmware for the M5Stack AtomS3R: an animated face with six expressions,
  button gestures, half-duplex audio, and an HTTP provisioning API served over
  a WPA2 setup access point
- One authenticated route with ten commands, whose advertised capabilities are
  generated from the command registry rather than written by hand
- Conversations against a configurable HTTP gateway, with the reply streamed
  back as Server-Sent Events carrying text, PCM audio, and per-sentence
  expressions — so the face changes mid-answer with no extra round trip
- The command API stays answerable during a conversation, because the
  conversation runs on its own task below the HTTP server's priority
- An access token kept in NVS, readable by holding the button, and replaced
  with `token.rotate`
- Host tests covering hardware-independent behavior, run on a PC with no board
  attached, plus an emulator check that boots the firmware image from the ROM
  bootloader and fails unless the startup log shows the self-checks passing
- Safe, debug, QEMU and release builds; a diagnostic gateway that needs no
  speech stack; and a flashing tool that captures the startup log from its
  first line
- Hardware facts behind the pin assignments and register sequences, with
  source locations and verification notes

### Known limitations

- Recording is a fixed three seconds — no voice activity detection, no wake word
- Plain HTTP on the local network; the token travels in a header
- Hardware verification was limited to a short run on one board, one network, and
  the safe configuration. The end-to-end conversation path was not verified on
  this tree
- No servo, no infrared, and no over-the-air update, though the
  `atoms3r-release` partition layout leaves room for one
