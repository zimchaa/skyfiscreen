#!/usr/bin/env bash
# Wait for the Presto to appear as a BOOTSEL mass-storage drive, then copy the
# firmware onto it. Run this, THEN put the Presto into BOOTSEL mode.
#
# BOOTSEL: unplug the Presto, hold its BOOT button, plug USB back in, release.
# It mounts as a drive named RP2350 / RPI-RP2 (contains INFO_UF2.TXT).
set -euo pipefail
cd "$(dirname "$0")"

UF2=build/skyfiscreen.uf2
[ -f "$UF2" ] || { echo "!! $UF2 not found — run ./build.sh first"; exit 1; }

echo ">> Waiting for a BOOTSEL drive (put the Presto into BOOTSEL now)…  Ctrl-C to abort"

find_mount() {
  for base in "/media/$USER" /run/media/"$USER" /media /mnt; do
    [ -d "$base" ] || continue
    for d in "$base"/*/ "$base"/*/*/; do
      [ -f "${d}INFO_UF2.TXT" ] && { echo "${d%/}"; return 0; }
    done
  done
  return 1
}

# Try to auto-mount a labelled device if udisks is available but nothing mounted.
try_udisks_mount() {
  command -v udisksctl >/dev/null || return 0
  local dev
  dev=$(lsblk -rno NAME,LABEL 2>/dev/null | awk '/RP2350|RPI-RP2/{print "/dev/"$1; exit}')
  [ -n "${dev:-}" ] && udisksctl mount -b "$dev" >/dev/null 2>&1 || true
}

MNT=""
for _ in $(seq 1 120); do   # ~60s
  if MNT=$(find_mount); then break; fi
  try_udisks_mount
  sleep 0.5
done

[ -n "$MNT" ] || { echo "!! No BOOTSEL drive found. Is the Presto in BOOTSEL mode?"; exit 1; }

echo ">> Found BOOTSEL drive at: $MNT"
echo ">> Copying $UF2 …"
cp "$UF2" "$MNT"/
sync
echo ">> Done. The Presto will reboot into the new firmware."
