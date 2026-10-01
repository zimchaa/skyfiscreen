# SkyFi Screen

C/C++ firmware for a **Pimoroni Presto** (RP2350B, 480×480 cap-touch, WiFi, piezo,
7-zone RGB LEDs) acting as the always-on physical control panel for the **SkyFi ground
station**. It sits alongside the Raspberry Pi and shows live environmental data, a WiFi-join
QR code, and a big **SAFETY LAND NOW** button — talking to the Pi's REST API.

Built on the [pimoroni/presto-boilerplate](https://github.com/pimoroni/presto-boilerplate)
foundation (pico-sdk + Pimoroni `ST7701` driver) with **LVGL** for the UI.

See the sibling `../sky-fi-app` (github zimchaa/skyfi-app) for the wider system:
its `server/` (skyfid) runs on the Pi, and `contracts/presto-link.md` defines the
USB serial protocol this firmware speaks.

## System

```
Enviro Weather (enviro/)  --Qw/ST I2C-->  Presto (this firmware)  --USB serial-->  Pi (skyfid)
  anemometer, vane, rain     BME280/LTR-559    tiles, LAND button       pilink JSON lines    web app, drone sim,
  as I2C regs @ 0x42         read directly     ground-station page      (WiFi = fallback)    auto-land
```

## Layout

```
src/            firmware sources (main + LVGL bridge + peripherals + net,
                pilink = USB link to the Pi, sensor_hub = Qw/ST weather sensors)
ui/             LVGL screens
enviro/         firmware for the Enviro Weather (I2C hub for wind/rain)
common/         shared between the two firmwares (enviro_hub_regs.h)
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

- **`patches/st7701-start-frame-xfer-hang.patch`**: *now merged upstream in
  pimoroni/presto — only needed for a presto checkout older than that.* Fixes a hang in
  Pimoroni's ST7701 driver — `start_frame_xfer()` exec'd an `out` on the
  parallel PIO SM while its FIFO was empty (autopull enabled), which latches
  EXEC_STALLED forever; `pio_sm_exec_wait_blocking()` then spins inside the
  scanout ISR and freezes the display core, leaving the panel blank. Replaced
  with `pio_sm_restart()` + a non-blocking `jmp`. Worth upstreaming to
  pimoroni/presto. Apply with: `git -C lib/presto apply ../../patches/st7701-start-frame-xfer-hang.patch`

## Build and flash from the Pi (no BOOT button needed)

The Pi (`4our.local`) has the toolchain and `picotool`; both boards are reflashed
over their USB connections. The BME280 driver needs its submodule:
`git -C lib/pimoroni-pico submodule update --init drivers/bme280/src`.

```bash
./build.sh && sudo picotool load -f --ser <presto serial> -x build/skyfiscreen.uf2
enviro/build.sh && sudo picotool load -f --ser <enviro serial> -x enviro/build/enviro_hub.uf2
```

Serials: `ls /dev/serial/by-id/`. Backups of the July 2026 firmware on both boards
are in `4our.local:~/skyfi/firmware-backups/` (restore with `picotool load -f -x`).

## Enviro hub (`enviro/`)

The Enviro Weather runs `enviro_hub`: an I2C target at **0x42** on the Qw/ST bus
exposing anemometer (3 s average + 60 s gust), wind vane and rain gauge as registers
(`common/enviro_hub_regs.h`; calibration from pimoroni/enviro). The Enviro's own
BME280 (0x77) and LTR-559 (0x23) share that bus and are read directly by the Presto.
A sensor that's missing shows `--` on the panel and is re-probed every few seconds;
it is never replaced with mock values.

## Live data over WiFi (fallback link)

The panel joins WiFi and polls the ground-station REST API when credentials are
baked in at build time; otherwise it runs on mock data. `SKYFI_API_HOST` must be
a dotted-quad IP (the Pi's — or your laptop's, for the mock server):

```bash
cmake -B build -DSKYFI_WIFI_SSID="YourAP" -DSKYFI_WIFI_PASSWORD="secret" \
      -DSKYFI_API_HOST=192.168.1.50    # then ./build.sh + flash
```

Run the mock Pi server on a machine on the same network:

```bash
cd mock-server
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/uvicorn app:app --host 0.0.0.0 --port 8000
```

Demo controls: `curl -X POST <host>:8000/demo/fault` toggles a gusting-wind
fault scenario; `/demo/reset` returns to nominal. LAND NOW on the panel does a
real `POST /api/v1/land` — watch the server log for the command id. Stopping
the server demonstrates the stale/offline states (pill + LEDs).

## Phases

- **Phase 0** — prove build/flash/display pipeline (minimal PicoGraphics test). ✅
- **Phase 1** — port LVGL; wire touch, buzzer, RGB LEDs, backlight. ✅
- **Phase 2** — the SkyFi panel UI (env tiles, WiFi QR, LAND NOW), mocked data. ✅
- **Phase 3** — WiFi + REST client against the mock Pi server. ✅
- **Phase 4** — USB link to the Pi (pilink), real Enviro sensors, ground-station page. ← current
