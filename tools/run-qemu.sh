#!/usr/bin/env bash
#
# Run the firmware under QEMU, with no board attached.
#
# Builds, merges a flash image, and boots it in QEMU. Device output is written
# to stdout. The PowerShell wrapper runs this script through WSL on Windows.
#
# Usage:
#   tools/run-qemu.sh --install-only     fetch QEMU and stop; needed once
#   tools/run-qemu.sh --install          fetch it first if absent, then run
#   tools/run-qemu.sh                    build, merge, boot, print the log
#   tools/run-qemu.sh --check            ...and fail if the boot did not finish
#   tools/run-qemu.sh --seconds 30
#   tools/run-qemu.sh --wait-for-gdb     start halted, wait on localhost:1234
#   tools/run-qemu.sh --image out.bin    boot an image instead of building one

set -euo pipefail

cd "$(dirname "$0")/.."

# The QEMU release to fetch. Raising it means booting the firmware on the new
# version and seeing it reach the end of startup, not just seeing it install.
QEMU_TAG='esp-develop-9.2.2-20260417'
QEMU_BUILD='esp_develop_9.2.2_20260417'
QEMU_HOME="${HOME}/qemu-esp"

PIO_ENV='atoms3r-qemu'
BUILD_DIR=".pio/build/${PIO_ENV}"

seconds=20
install=0
install_only=0
skip_build=0
check=0
wait_for_gdb=0
image=''

while [ $# -gt 0 ]; do
  case "$1" in
    --install)      install=1 ;;
    --install-only) install=1; install_only=1 ;;
    --skip-build)   skip_build=1 ;;
    --check)        check=1 ;;
    --wait-for-gdb) wait_for_gdb=1 ;;
    --seconds)      seconds="$2"; shift ;;
    --image)        image="$2"; skip_build=1; shift ;;
    -h|--help)      sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

say() { printf '\n=== %s ===\n' "$1"; }

# ---------------------------------------------------------------- install

qemu_asset() {
  case "$(uname -s)-$(uname -m)" in
    Linux-aarch64)  echo "aarch64-linux-gnu" ;;
    Linux-x86_64)   echo "x86_64-linux-gnu" ;;
    Darwin-arm64)   echo "aarch64-apple-darwin" ;;
    Darwin-x86_64)  echo "x86_64-apple-darwin" ;;
    *) echo "no QEMU build is published for $(uname -s) $(uname -m)" >&2; exit 1 ;;
  esac
}

if [ "$install" = 1 ]; then
  say "installing QEMU into ${QEMU_HOME}"
  asset="qemu-xtensa-softmmu-${QEMU_BUILD}-$(qemu_asset).tar.xz"

  mkdir -p "$QEMU_HOME"
  ( cd "$QEMU_HOME"
    if [ ! -x ./qemu/bin/qemu-system-xtensa ]; then
      echo "  fetching ${asset}"
      curl -fsSL -o q.tar.xz \
        "https://github.com/espressif/qemu/releases/download/${QEMU_TAG}/${asset}"
      tar -xf q.tar.xz
      rm -f q.tar.xz
    fi

    # libSDL2 and libslirp are hard dependencies recorded in DT_NEEDED. Without
    # them the binary does not start at all, so this is not optional.
    if command -v ldd >/dev/null && ldd ./qemu/bin/qemu-system-xtensa | grep -q 'not found'; then
      echo "  installing the missing shared libraries (sudo may ask for a password)"
      sudo=
      if [ "$(id -u)" -ne 0 ]; then sudo=sudo; fi
      export DEBIAN_FRONTEND=noninteractive
      $sudo apt-get update -qq
      $sudo apt-get install -y -qq --no-install-recommends libsdl2-2.0-0 libslirp0
    else
      echo "  dependencies already satisfied"
    fi

    ./qemu/bin/qemu-system-xtensa --version | head -1
  )
  if [ "$install_only" = 1 ]; then
    exit 0
  fi
fi

qemu="${QEMU_HOME}/qemu/bin/qemu-system-xtensa"
if [ ! -x "$qemu" ]; then
  echo "QEMU is not installed. Run this script once with --install." >&2
  exit 1
fi

# ------------------------------------------------------------------ build

# PlatformIO keeps its packages here. The environment variable is PlatformIO's
# own, so honouring it means this script keeps working for anyone who has moved
# that directory rather than only for the default layout.
PIO_CORE="${PLATFORMIO_CORE_DIR:-${HOME}/.platformio}"

find_pio() {
  if command -v pio >/dev/null; then command -v pio; return; fi
  if [ -x "${PIO_CORE}/penv/bin/pio" ]; then echo "${PIO_CORE}/penv/bin/pio"; return; fi
  echo "PlatformIO Core was not found. See https://docs.platformio.org/en/latest/core/installation/" >&2
  exit 1
}

if [ "$skip_build" = 0 ]; then
  say "building (${PIO_ENV})"
  "$(find_pio)" run -e "$PIO_ENV"
fi

# ------------------------------------------------------------------ merge

# QEMU starts from the ROM bootloader, exactly as the chip does, so it needs
# the whole flash rather than the application alone. Handed just the
# application it would find no second-stage bootloader and no partition table,
# and would never reach any of this project's code.
merge_image() {
  local out="${BUILD_DIR}/flash_image.bin"
  local args=(--chip esp32s3 merge_bin --fill-flash-size 8MB -o "$out"
              0x0     "${BUILD_DIR}/bootloader.bin"
              0x8000  "${BUILD_DIR}/partitions.bin"
              0x10000 "${BUILD_DIR}/firmware.bin")

  # esptool comes from PlatformIO's own package when there is one, because that
  # is the version the build was made with. Failing that, whatever is installed.
  #
  # Any Python will do to run it. PlatformIO installed from pip has no penv, so
  # requiring one would fail on exactly the machines CI runs on; esptool.py puts
  # its own bundled dependencies on sys.path before importing anything, and the
  # only one it expects from the environment is pyserial, which PlatformIO
  # itself depends on.
  local pio_esptool="${PIO_CORE}/packages/tool-esptoolpy/esptool.py"
  local python="python3"
  [ -x "${PIO_CORE}/penv/bin/python" ] && python="${PIO_CORE}/penv/bin/python"

  if [ -f "$pio_esptool" ]; then
    "$python" "$pio_esptool" "${args[@]}"
  elif command -v esptool.py >/dev/null; then
    esptool.py "${args[@]}"
  elif python3 -c 'import esptool' >/dev/null 2>&1; then
    python3 -m esptool "${args[@]}"
  else
    echo "esptool was not found. It ships with PlatformIO; 'pip install esptool' also works." >&2
    exit 1
  fi

  echo "$out"
}

if [ -n "$image" ]; then
  say "using ${image}"
else
  say "merging the flash image"
  merge_image >/dev/null
  image="${BUILD_DIR}/flash_image.bin"
fi

# ------------------------------------------------------------------- boot

say "starting QEMU"

# -m 8M with is_octal describes the AtomS3R's 8 MB octal PSRAM. strap_mode 0x04
# is the pin state that means "boot from SPI flash".
qemu_args=(-nographic
           -machine esp32s3
           -m 8M
           -global driver=ssi_psram,property=is_octal,value=true
           -global driver=esp32s3.gpio,property=strap_mode,value=0x04
           -drive "file=${QEMU_HOME}/flash_image.bin,if=mtd,format=raw")

if [ "$wait_for_gdb" = 1 ]; then
  qemu_args+=(-s -S)
  # Attaching a debugger takes longer than the default run.
  [ "$seconds" = 20 ] && seconds=600
  echo "  waiting for a debugger on localhost:1234. The CPU is halted."
fi

cp -f "$image" "${QEMU_HOME}/flash_image.bin"
echo "  stopping after ${seconds} seconds."
echo

timeout_cmd=timeout
command -v timeout >/dev/null || timeout_cmd=gtimeout
command -v "$timeout_cmd" >/dev/null || {
  echo "neither timeout nor gtimeout is available (on macOS: brew install coreutils)" >&2
  exit 1
}

log="$(mktemp)"
trap 'rm -f "$log"' EXIT

# QEMU is stopped by the timeout rather than by the firmware, so a non-zero
# exit here is expected and says nothing. What the run proved is in the log.
( cd "$QEMU_HOME" && "$timeout_cmd" "$seconds" "$qemu" "${qemu_args[@]}" ) 2>&1 | tee "$log" || true

# ------------------------------------------------------------------ check

if [ "$check" = 0 ]; then
  say "finished"
  exit 0
fi

say "checking what the boot produced"

status=0

# Reaching this means every stage of startup ran and each one agreed with the
# state it found. It is printed after the self-checks, so its absence covers
# both "a check failed" and "startup never got that far".
if grep -q 'selftest: all ok' "$log"; then
  echo "  ok    self-checks passed"
else
  echo "  FAIL  'selftest: all ok' never appeared"
  status=1
fi

# The HTTP server is the last thing to come up, so this is the marker for a
# complete startup rather than one that stalled after the self-checks.
if grep -q 'ready\. press the button\.' "$log"; then
  echo "  ok    startup finished"
else
  echo "  FAIL  the device never reported itself ready"
  status=1
fi

# A panic prints this and then reboots, so without the check a crash loop can
# still show a passing self-check from the boot before it.
if grep -qE 'Guru Meditation|CORRUPT HEAP|assert failed' "$log"; then
  echo "  FAIL  the firmware panicked:"
  grep -nE 'Guru Meditation|CORRUPT HEAP|assert failed' "$log" | head -5 | sed 's/^/          /'
  status=1
else
  echo "  ok    no panic"
fi

if [ "$status" = 0 ]; then
  say "boot check passed"
else
  say "boot check FAILED"
fi
exit "$status"
