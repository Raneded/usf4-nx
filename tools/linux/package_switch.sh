#!/usr/bin/env bash
# Lays out the Switch SD card folder in out/nx/sdcard/switch/usf4-nx: the NRO and the shader package, plus a README.
# The game files are yours and stay out of it: copy your extracted disc (assets/game_root, WITHOUT its $SystemUpdate
# folder, which the game never reads) to sdmc:/switch/usf4-nx/game_root. Counterpart of sa2-nx's
# tools/win/package_switch.ps1.
#
#   tools/linux/package_switch.sh            -> out/nx/sdcard/switch/usf4-nx and out/nx/usf4-nx-switch.zip
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
nro="$root/out/nx/usf4.nro"
[ -f "$nro" ] || { echo "$nro missing: build the Switch target first (docs/building.md, Switch)" >&2; exit 1; }
for f in sa2_shaders.mesp sa2_shaders.mesp.idx; do
  [ -f "$root/out/shaders/$f" ] || { echo "out/shaders/$f missing: build the shader package first" >&2; exit 1; }
done
dest="$root/out/nx/sdcard/switch/usf4-nx"
rm -rf "$root/out/nx/sdcard"
mkdir -p "$dest"
cp "$nro" "$root/out/shaders/sa2_shaders.mesp" "$root/out/shaders/sa2_shaders.mesp.idx" "$dest/"
cat > "$dest/README.txt" <<'TXT'
usf4-nx: Ultra Street Fighter IV (Xbox 360) on the Switch, test build.

Put this folder on the SD card as sdmc:/switch/usf4-nx, and copy your own extracted disc next to the NRO:
    sdmc:/switch/usf4-nx/game_root/      <- assets\game_root from the PC build, without its $SystemUpdate folder
                                           (Default.xex, archive\, dl0\, dl1\, dl2\, movie\, ... ; about 7.6 GB)

Launch with a title takeover (hold R while starting a game, then open the Homebrew Menu), or a forwarder with full
memory. Not from the Album: it has too little memory.

The first start writes usf4.toml (your options) next to the NRO and keeps a log in logs/ (send it back after a test).
The first time each scene is drawn, the renderer compiles its pipelines: short stutters, saved in cache/ for the next
time.
TXT
(cd "$root/out/nx/sdcard" && rm -f ../usf4-nx-switch.zip && python3 -I -c "
import os,zipfile
with zipfile.ZipFile('../usf4-nx-switch.zip','w',zipfile.ZIP_DEFLATED) as z:
    for d,_,fs in os.walk('switch'):
        for f in fs: z.write(os.path.join(d,f))
")
ls -la "$dest" "$root/out/nx/usf4-nx-switch.zip"
