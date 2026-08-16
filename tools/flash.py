#!/usr/bin/env python3
"""Flash a board, capture startup from reset, and check the resulting log.

The script keeps the serial port open across the reset it initiates, avoiding
early USB CDC output that can be missed when upload and monitoring are started
separately.

The first-created access token may be printed before capture starts. To
retrieve the current token, open a monitor and hold the device button for
about half a second while the device is idle.

Usage
-----
    python tools/flash.py                     build, flash, capture, check
    python tools/flash.py --env atoms3r-debug
    python tools/flash.py --no-flash          just reset and capture
    python tools/flash.py --port COM7 --seconds 60

Related commands:

    pio run -e atoms3r-safe -t erase     forget the token and the credentials,
                                         so the next boot is a first boot
    pio device monitor                   watch it for as long as you like

Note that a monitor and the flasher cannot hold the port at the same time.
Close one before starting the other.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_pio():
    """PlatformIO Core, from PATH or the directory the IDE installs it in."""
    found = shutil.which("pio")
    if found:
        return found
    core = os.environ.get("PLATFORMIO_CORE_DIR") or os.path.expanduser("~/.platformio")
    for candidate in ("penv/Scripts/pio.exe", "penv/bin/pio"):
        path = os.path.join(core, *candidate.split("/"))
        if os.path.isfile(path):
            return path
    sys.exit("PlatformIO Core was not found. "
             "See https://docs.platformio.org/en/latest/core/installation/")


# Espressif's USB vendor id. The ESP32-S3 presents its built-in USB
# Serial/JTAG under it, which is what picks the board out from the virtual
# ports Windows keeps for Bluetooth -- there are usually two of those, so
# "the only port" is almost never a useful rule.
kEspressifVendorId = 0x303A


def find_port(requested):
    """The board's serial port."""
    from serial.tools import list_ports

    if requested:
        return requested
    ports = list(list_ports.comports())
    if not ports:
        sys.exit("No serial port found. Is the board plugged in?\n"
                 "  If it is, hold the reset button for about two seconds and "
                 "release it when the LED turns green, which enters download mode.")

    espressif = [p for p in ports if p.vid == kEspressifVendorId]
    if len(espressif) == 1:
        return espressif[0].device
    if not espressif and len(ports) == 1:
        return ports[0].device

    listing = "\n".join("  %s  %s" % (p.device, p.description)
                        for p in (espressif or ports))
    sys.exit("Cannot tell which port is the board; say which with --port.\n" + listing)


def capture(port_name, seconds):
    """Reset the board and read its output for `seconds`, echoing as it comes."""
    import serial

    try:
        port = serial.Serial(port_name, 115200, timeout=0.2)
    except serial.SerialException as error:
        sys.exit("Could not open %s: %s\n"
                 "  A monitor holding the port will do this. Close it first."
                 % (port_name, error))

    with port:
        # Opening the port can itself reset the board: the driver asserts the
        # control lines, and on this one they are wired to reset and to the
        # boot pin. So settle, discard whatever that produced, and then reset
        # deliberately -- otherwise the capture might hold two boots and look
        # like a device that is restarting in a loop.
        time.sleep(0.3)

        # RTS drives reset, DTR drives the boot pin. DTR low keeps it out of
        # download mode; pulsing RTS restarts it into the application.
        port.dtr = False
        port.rts = True
        time.sleep(0.1)
        port.reset_input_buffer()
        port.rts = False

        lines, buffer = [], b""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            buffer += port.read(4096)
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                print(line)
                lines.append(line)
    return lines


def report(lines):
    """Say whether the boot finished. Returns an exit code."""
    text = "\n".join(lines)
    status = 0

    def check(ok, good, bad):
        nonlocal status
        print("  %s  %s" % ("ok  " if ok else "FAIL", good if ok else bad))
        if not ok:
            status = 1

    print("\n=== what the boot produced ===")

    # The same three the emulator run checks, so the two are comparable.
    check("selftest: all ok" in text,
          "self-checks passed", "'selftest: all ok' never appeared")
    check("ready. press the button." in text,
          "startup finished", "the device never reported itself ready")
    panics = [l for l in lines
              if "Guru Meditation" in l or "CORRUPT HEAP" in l or "assert failed" in l]
    check(not panics, "no panic", "the firmware panicked: " + (panics[0] if panics else ""))

    # Only meaningful on a board. The emulator has no PSRAM to initialise.
    check("PSRAM initialized=true" in text,
          "PSRAM initialised", "PSRAM did not initialise")

    # A board that restarts mid-capture said 'bootstrap' more than once. Worth
    # its own check: a crash loop can still show one passing self-check.
    boots = text.count("StackChan firmware bootstrap")
    check(boots == 1, "booted once",
          "started %d times during the capture, so it is restarting" % boots)

    # Reported rather than checked: what is attached is a property of the
    # bench, not of the firmware.
    for note in ("new access token:", "access token loaded", "display init failed",
                 "voice base not available"):
        for line in lines:
            if note in line:
                print("  note  " + line[line.index(note):])
                break

    if "access token" not in text:
        # The line is normally absent, because the boot that creates the token
        # is the one esptool triggers when it finishes writing.
        print("\n  To read the access token, hold the button for half a second "
              "with a monitor open.")

    print("\n=== boot check %s ===" % ("passed" if status == 0 else "FAILED"))
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--env", default="atoms3r-safe",
                        help="PlatformIO environment (default: atoms3r-safe, the one "
                             "to use first: INFO logging and no one-way fuses)")
    parser.add_argument("--port", help="serial port; detected when there is only one")
    parser.add_argument("--seconds", type=int, default=30,
                        help="how long to capture (default: 30)")
    parser.add_argument("--no-flash", action="store_true",
                        help="skip flashing; reset the board and capture only")
    arguments = parser.parse_args()

    try:
        import serial  # noqa: F401
    except ImportError:
        sys.exit("pyserial is needed to read the port: pip install pyserial\n"
                 "  PlatformIO's own environment already has it.")

    if not arguments.no_flash:
        print("=== building and flashing (%s) ===" % arguments.env)
        command = [find_pio(), "run", "-e", arguments.env, "-t", "upload"]
        if arguments.port:
            command += ["--upload-port", arguments.port]
        if subprocess.run(command, cwd=ROOT).returncode != 0:
            return 1

    port_name = find_port(arguments.port)
    print("\n=== capturing from %s for %ds ===\n" % (port_name, arguments.seconds))
    return report(capture(port_name, arguments.seconds))


if __name__ == "__main__":
    sys.exit(main())
