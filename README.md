# SkyFi Screen

C/C++ firmware for a **Pimoroni Presto** (RP2350B, 480×480 cap-touch, WiFi, piezo,
7-zone RGB LEDs) acting as the always-on physical control panel for the **SkyFi ground
station**. It sits alongside the Raspberry Pi and shows live environmental data, a WiFi-join
QR code, and a big **SAFETY LAND NOW** button — talking to the Pi's REST API.

Built on the [pimoroni/presto-boilerplate](https://github.com/pimoroni/presto-boilerplate)
foundation (pico-sdk + Pimoroni `ST7701` driver) with **LVGL** for the UI.

See the sibling `../skyfiapp` for the wider system (the "Virtual Mast Wizard").

## Layout

```
src/            firmware sources (main + LVGL bridge + peripherals + net)
ui/             LVGL screens
lib/            dependencies (git clones): pico-sdk, pimoroni-pico, presto, lvgl
mock-server/    FastAPI stand-in for the Pi's REST API (Phase 3+)
vendor/         reference: upstream presto-boilerplate
```

## Prerequisites (one-time, needs sudo)

```bash
sudo apt update && sudo apt install -y cmake ninja-build build-essential \
  gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
  python3-venv git
```

## Build

```bash
./build.sh            # configures (first run) + builds build/skyfiscreen.uf2
```

## Flash

1. Reboot the Presto into **BOOTSEL**: hold the BOOT button and tap RESET (or replug
   while holding BOOT). It mounts as a USB drive named `RP2350` / `RPI-RP2`.
2. Copy the UF2 onto it:
   ```bash
   cp build/skyfiscreen.uf2 /media/$USER/RP2350/    # path may vary
   ```
   The Presto reboots into the new firmware automatically.

Serial debug output (`printf`) is available over USB at `/dev/ttyACM*` (the
number can change across replug cycles).

## Local patches to dependencies

The `lib/` clones are not committed; anything we fix in them lives in `patches/`
and must be re-applied after a fresh clone:

- **`patches/st7701-start-frame-xfer-hang.patch`** (required): fixes a hang in
  Pimoroni's ST7701 driver — `start_frame_xfer()` exec'd an `out` on the
  parallel PIO SM while its FIFO was empty (autopull enabled), which latches
  EXEC_STALLED forever; `pio_sm_exec_wait_blocking()` then spins inside the
  scanout ISR and freezes the display core, leaving the panel blank. Replaced
  with `pio_sm_restart()` + a non-blocking `jmp`. Worth upstreaming to
  pimoroni/presto. Apply with: `git -C lib/presto apply ../../patches/st7701-start-frame-xfer-hang.patch`

## Phases

- **Phase 0** — prove build/flash/display pipeline (minimal PicoGraphics test). ← current
- **Phase 1** — port LVGL; wire touch, buzzer, RGB LEDs, backlight.
- **Phase 2** — the SkyFi panel UI (env tiles, WiFi QR, LAND NOW), mocked data.
- **Phase 3** — WiFi + REST client against the mock Pi server.
- **Phase 4** — wired backup link + polish.
