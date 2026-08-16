# Hardware configuration used by the firmware

This document records the hardware values used by the current implementation.
It is not a general compatibility list.

## Supported assembly

- M5Stack AtomS3R
- 8 MB flash
- 8 MB octal PSRAM
- 128 × 128 integrated display
- M5Stack Atomic Voice Base

The board definition is
[`src/config/boards/atoms3r.json`](../../src/config/boards/atoms3r.json).

## Pin assignments

### Integrated devices

| GPIO | Use |
|---:|---|
| 0 | Internal I2C SCL |
| 45 | Internal I2C SDA |
| 41 | User button, active low |
| 47 | Infrared transmit; not used by this firmware |
| 48, 42, 21, 15, 14 | Display |
| 19, 20 | USB |
| 43 | Reserved by the board profile |

### Voice-base bottom connection

| GPIO | Use |
|---:|---|
| 39 | Voice-base I2C SCL |
| 38 | Voice-base I2C SDA |
| 5 | I2S playback data, device to codec |
| 6 | I2S word select |
| 7 | I2S capture data, codec to device |
| 8 | I2S bit clock |

The voice base uses all six bottom pins. The board-profile code treats them as
unavailable for another application while the base is selected.

### Grove connector

GPIO 1 and GPIO 2 are exposed on the Grove connector. The current firmware
does not assign them to a peripheral. The board profile allows one declared
use at a time, such as I2C, UART, or servo PWM.

The connector supplies 5 V power, but the GPIO logic level remains 3.3 V.

## I2C buses

| Bus purpose | Pins | Controller use in current firmware |
|---|---|---|
| Internal devices | SCL 0, SDA 45 | Startup scan, then display/backlight library |
| Voice base | SCL 39, SDA 38 | Startup scan, then audio adapter |

Startup probing uses I2C controller 0 for each physical bus in sequence and
releases it after each scan. The display and audio adapters initialize after
the probe.

### Devices on the scanned buses

| Device | Address | Use or identification |
|---|---:|---|
| BMI270 motion sensor | 0x68 or 0x69 | Register 0x00 equals 0x24 |
| ES8311 audio codec | 0x18 | Registers 0xFD/0xFE equal 0x83/0x11 |
| Voice-base I/O expander | 0x43 | Used by the audio adapter for speaker output |

The probe identifies the motion sensor and codec from their registers. It
reports other responding addresses without assigning a device type; the audio
adapter separately uses the I/O expander at 0x43.

## Audio

### Format

| Property | Value |
|---|---|
| Sample rate | 16,000 Hz |
| Sample type | Signed 16-bit little-endian |
| Channels | Mono |
| Recording length | Three seconds |
| Capture buffer | 48,000 samples / 96,000 bytes |
| Gateway audio-event limit | 4,096 decoded bytes |

No MCLK pin is wired by this assembly. The codec uses the bit clock as its
clock input. At 16 kHz with 16-bit stereo slots, BCLK is 512 kHz.

### Direction

The current audio adapter is half duplex:

```text
idle -> capture -> idle
idle -> playback -> idle
```

Capture and playback channels are not opened together. The adapter serializes
direction changes because the conversation task and main loop can both request
audio.

For capture, only the left I2S slot is selected. Playback uses both slots for
mono output.

### Codec and amplifier control

The codec and I/O-expander register sequences are defined in
[`src/platform/stackchan_audio/src/voice_base.cpp`](../../src/platform/stackchan_audio/src/voice_base.cpp).
That file is the implementation source for register values.

The amplifier is enabled only for playback. The audio adapter runs a startup
tone and a short microphone check when initialization succeeds.

## Display

The firmware uses the managed display component pinned in
[`src/platform/stackchan_display/idf_component.yml`](../../src/platform/stackchan_display/idf_component.yml).

AtomS3R units may contain either a GC9107 or ST7735 display controller. The
display component detects the board/panel at startup and applies the matching
geometry. Application code does not select one controller by hand.

The face uses a 128 × 128 canvas and six expressions:

```text
neutral happy sad doubt sleepy angry
```

The canvas remains in internal RAM so display transfer can use DMA.

## Button

GPIO 41 is configured as an active-low input with its pull-up enabled.
Debouncing and click/hold recognition are implemented in `src/core`.

| Gesture | Current behavior |
|---|---|
| Single press | Start a conversation |
| Double press | Display startup probe information |
| Triple press | Display IP address and log device information |
| Hold while idle | Log device information and API token |
| Hold during a conversation | Cancel the conversation |

## Sensors and unused hardware

The startup probe can identify the BMI270, but the firmware does not expose
motion, acceleration, or orientation features.

The infrared transmitter, Grove connector, and servo control are not used by
the current firmware.

## Initialization constraints

The implementation relies on this order:

1. Scan internal and voice-base I2C buses.
2. Initialize display.
3. Initialize audio.
4. Start Wi-Fi and the application services.

A later runtime scan is not performed because the display and audio adapters
own the buses after startup.

## Verification notes

When changing pins, bus ownership, display dependency, or audio register
values, record:

- board and base revision
- firmware environment and commit
- startup probe output
- capture sample count and RMS
- playback result
- display controller reported at startup

Current source locations:

- Board claims: [`src/core/stackchan_board/include/stackchan/board/profile.hpp`](../../src/core/stackchan_board/include/stackchan/board/profile.hpp)
- Probe: [`src/platform/stackchan_probe/src/hardware.cpp`](../../src/platform/stackchan_probe/src/hardware.cpp)
- Audio: [`src/platform/stackchan_audio/src/voice_base.cpp`](../../src/platform/stackchan_audio/src/voice_base.cpp)
- Display: [`src/platform/stackchan_display/src/m5gfx_face.cpp`](../../src/platform/stackchan_display/src/m5gfx_face.cpp)
- Button: [`src/platform/stackchan_input/src/button.cpp`](../../src/platform/stackchan_input/src/button.cpp)

Hardware identification and product specifications should also be checked
against the documentation for the exact board/base revision being tested.
