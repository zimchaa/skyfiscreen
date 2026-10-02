#!/usr/bin/env bash
# Recovery flash: wait (10 min) for an RP2350 in BOOTSEL (2e8a:000f), then load
# the Presto firmware. Use when the running firmware is hung and picotool -f
# cannot reboot it: start this, then hold BOOT + tap RESET on the Presto.
UF2=${1:-$(dirname "$0")/../build/skyfiscreen.uf2}
for i in $(seq 1 600); do
  if lsusb | grep -q "2e8a:000f"; then
    sleep 1
    sudo picotool load -x "$UF2" 2>&1 | grep -v "Loading into"
    exit $?
  fi
  sleep 1
done
echo "timeout: no BOOTSEL device"; exit 1
