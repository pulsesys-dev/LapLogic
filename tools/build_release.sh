#!/usr/bin/env bash
# Builds a LapLogic release: both firmwares, the files for flashing over USB and for updates over
# Wi-Fi, the flasher page and the package zip. Used by .github/workflows/release.yml; also runs
# locally once arduino-cli, the ESP32 core 2.0.17 and the libraries are installed (see docs/building.md).
#
#   tools/build_release.sh            -> build/release/
#
# Environment (all optional):
#   ARDUINO_CLI  arduino-cli command      (default: arduino-cli)
#   ESPTOOL      esptool command          (default: python3 -m esptool)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI=${ARDUINO_CLI:-arduino-cli}
ESPTOOL=${ESPTOOL:-python3 -m esptool}
OUT="$ROOT/build"
REL="$OUT/release"
VERSION=$(sed -n 's/^#define APP_VERSION *"\([^"]*\)".*/\1/p' "$ROOT/LapLogic/config.h")
echo "LapLogic $VERSION"
mkdir -p "$REL"
rm -f "$REL"/*

# LVGL is compiled with the lv_conf.h in the Arduino libraries folder, so put ours there
LIBDIR="$($CLI config get directories.user 2>/dev/null || echo "$HOME/Arduino")/libraries"
mkdir -p "$LIBDIR"
cp "$ROOT/LapLogic/lv_conf.h" "$LIBDIR/lv_conf.h"

BOOT_APP0=$(find "$($CLI config get directories.data 2>/dev/null || echo "$HOME/.arduino15")" "$HOME/Arduino/hardware" \
                 -path '*esp32*' -name boot_app0.bin 2>/dev/null | head -1)
[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found - is the ESP32 core installed?"; exit 1; }

build() {   # fqbn  chip  bootloader-offset  folder  file-name
  local fqbn=$1 chip=$2 boot=$3 dir=$4 name=$5
  echo "== $name"
  "$CLI" compile --fqbn "$fqbn" --build-path "$OUT/$dir" "$ROOT/LapLogic"
  mkdir -p "$ROOT/firmware/$dir"
  for f in LapLogic.ino.bin LapLogic.ino.bootloader.bin LapLogic.ino.partitions.bin; do cp "$OUT/$dir/$f" "$ROOT/firmware/$dir/"; done
  cp "$BOOT_APP0" "$ROOT/firmware/$dir/boot_app0.bin"
  (cd "$ROOT/firmware/$dir" && $ESPTOOL --chip "$chip" merge_bin -o "../LapLogic-$name.bin" \
      --flash_mode keep --flash_freq keep --flash_size keep \
      "$boot" LapLogic.ino.bootloader.bin 0x8000 LapLogic.ino.partitions.bin 0xe000 boot_app0.bin 0x10000 LapLogic.ino.bin)
  cp "$ROOT/firmware/LapLogic-$name.bin" "$REL/"                       # for USB (flasher, esptool) at 0x0
  cp "$ROOT/firmware/$dir/LapLogic.ino.bin" "$REL/LapLogic-$name-ota.bin"  # for updates over Wi-Fi
}

build "espressif:esp32:esp32:FlashSize=4M,PSRAM=disabled,PartitionScheme=min_spiffs" \
      esp32 0x1000 cyd-esp32-2432s028r CYD-ESP32-2432S028R
build "espressif:esp32:esp32s3:FlashSize=4M,PSRAM=opi,PartitionScheme=min_spiffs,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio" \
      esp32s3 0x0 guition-jc4827w543 Guition-JC4827W543

sed -i.bak "s/^LapLogic [vb]*[0-9.]* firmware/LapLogic $VERSION firmware/" "$ROOT/firmware/OFFSETS.txt" && rm -f "$ROOT/firmware/OFFSETS.txt.bak"
python3 "$ROOT/tools/build_webpage.py" >/dev/null
python3 "$ROOT/tools/build_flasher.py"
cp "$ROOT/LapLogic-Flasher.html" "$REL/"

# the whole package, as people unzip it
(cd "$(dirname "$ROOT")" && zip -qr "$REL/LapLogic-$VERSION.zip" "$(basename "$ROOT")" \
    -x "*/build/*" "*/.git/*" "*/.pio/*" "*.DS_Store" "*__pycache__*")
echo "Release files in build/release:"
ls -l "$REL"
