# Current firmware architecture

This document describes the code in the current repository. It is not a list
of every capability of the board and does not describe planned features.

## Scope

The firmware currently supports:

- AtomS3R display and button
- an attached M5Stack Atomic Voice Base
- Wi-Fi station and setup-access-point modes
- a local HTTP command API
- a three-second audio conversation through a configured HTTP gateway

Servo control, infrared control, OTA handling, wake-word detection, voice
activity detection, Bluetooth features, and sensor applications are not
implemented.

## Source layout

```text
src/
  core/
    stackchan_domain/    protocol values, parsers, state, and policies
    stackchan_ports/     audio, face, and parameter interfaces
    stackchan_app/       request routing, discovery, and conversation parsing
    stackchan_runtime/   deadline, cancellation, mailbox, and buffer helpers
    stackchan_board/     board-profile and pin-claim rules
  platform/
    stackchan_audio/         voice-base capture and playback
    stackchan_commands/      command handlers and registration
    stackchan_conversation/  gateway HTTP client and conversation task
    stackchan_deviceapi/     HTTP server and provisioning routes
    stackchan_display/       face and setup screens
    stackchan_identity/      device, boot, build, and token identity
    stackchan_input/         button input
    stackchan_net/           Wi-Fi and provisioning state
    stackchan_probe/         startup I2C detection
    stackchan_selftest/      startup checks
    stackchan_ui/            button and screen behavior
  main/
    main.cpp              composition and main loop
```

Dependencies point from platform adapters to core. `src/main` constructs the
concrete adapters and passes them to the abstractions they implement.

The `native` build can see only `src/core`. This makes the protocol, parsers,
timing rules, and state behavior testable without firmware or a board.

## Startup order

`app_main` performs the following operations:

1. Log memory and validate the selected board profile.
2. Scan the internal and external I2C buses.
3. Initialize the display and button.
4. Load or generate the API token.
5. Initialize the voice base and run startup audio checks.
6. Start Wi-Fi.
7. Load the gateway URL and start the conversation task when audio is
   available.
8. Register commands and their availability.
9. Run the startup self-test.
10. Start the HTTP server.
11. Create the UI presenter and enter the 10 ms main loop.

The I2C scan runs before the display and audio adapters claim their buses.

## Main loop

The main loop:

- polls and interprets button gestures
- updates setup, notice, and face screens
- advances face animation
- advances the Wi-Fi connection policy
- logs memory every five seconds

It does not run a conversation or serve HTTP requests directly.

## Tasks and shared state

### Conversation task

The conversation task runs on core 1 at priority 4. It owns the long-running
record, HTTP, stream-parse, and playback sequence. The current phase is exposed
as an atomic value:

```text
idle -> listening -> thinking -> speaking -> idle
```

The HTTP server remains on its own task and can process commands while the
conversation task is active.

### HTTP server

The server installs these routes:

```text
POST /api/v1/command
GET  /
GET  /scan
POST /save
GET  /info
```

Command authorization, parsing, dispatch, and response construction are
delegated to `RequestRouter` in core.

### Cancellation

A shared atomic cancellation source carries ordinary cancellation and
emergency stop. The conversation checks it between bounded reads and writes.
Ordinary cancellation is cleared after the turn; emergency stop requires
`estop.clear` during the current boot.

## Conversation flow

```text
button or conversation.start
  -> record 3 seconds at 16 kHz mono, unless text was supplied
  -> POST WAV or JSON to <gateway_url>/v1/converse
  -> parse event envelopes from an SSE response
  -> stage PCM by sentence
  -> update expression and play audio
  -> require conversation.finished
  -> return to idle
```

Recording and playback are half duplex. The capture channel is closed before
the playback channel opens.

The gateway client uses:

- 10 seconds to receive response headers
- 30 seconds without received stream data before timeout
- one-second reads so cancellation can be observed
- HTTP status 200 for a successful stream

## Wi-Fi and provisioning

The network policy attempts to use credentials stored in NVS. If credentials
are absent, or repeated connection attempts fail, it starts a WPA2 setup
access point.

Before opening that access point, the firmware performs a bounded network scan
and caches up to 20 results. Provisioning requests are accepted only when the
request arrived through the setup access point.

Saving credentials is asynchronous: `POST /save` stores them, and a later
network tick begins the station connection.

## Memory use

The conversation task allocates two persistent PSRAM buffers at startup:

| Buffer | Size and purpose |
|---|---|
| Capture | 48,000 16-bit samples; three seconds at 16 kHz |
| Playback stage | 192,000 16-bit samples; up to twelve seconds |

The display canvas uses internal RAM so the display transfer can use DMA.
Request, JSON, SSE, and audio-event parsing use fixed-capacity buffers.

## Capability discovery

Commands are registered during startup. `device.describe` generates its
capability list from the registry. Commands that need missing hardware remain
listed with `available: false` and a reason.

The startup probe identifies:

- a motion sensor on the internal I2C bus when its chip ID matches
- the audio codec on the external I2C bus when its version registers match
- whether the external bus was checked and whether a voice base was found

Probe results describe the startup state; the firmware does not continuously
rescan occupied buses.

## Build configurations

| Environment | Relevant difference |
|---|---|
| `atoms3r-safe` | Debug build, safe-mode definition, development partition table |
| `atoms3r-debug` | Debug build for hardware development |
| `atoms3r-qemu` | Safe mode, UART0 console, Wi-Fi disabled |
| `atoms3r-release` | Release build and OTA-capable partition layout |
| `native` | Host-only core tests and analysis |

The OTA-capable partition layout reserves slots, but the firmware does not
implement an OTA update service.

## Related documents

- [Device interface](../api/device-interface.md)
- [Design principles](design-principles.md)
- [Hardware configuration](../hardware/pins-and-peripherals.md)
