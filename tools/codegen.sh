#!/usr/bin/env bash
# Translates Default.xex to C++ (Linux counterpart of tools/codegen.ps1, classic flavour, no post-steps yet).
#   tools/codegen.sh            -> app/generated, log in out/codegen.log
# REXGLUE overrides the generator path (default: sdk/out/linux-amd64/rexglue, built from out/host).
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
rexglue="${REXGLUE:-$root/sdk/out/linux-amd64/rexglue}"
[ -x "$rexglue" ] || { echo "rexglue not found at $rexglue; build it: cmake --build out/host --target rexglue" >&2; exit 1; }
[ -f "$root/assets/game_root/Default.xex" ] || { echo "assets/game_root/Default.xex missing; run tools/extract_iso.py" >&2; exit 1; }
mkdir -p "$root/out"
cd "$root/app"
if ! REX_MAX_JUMP_TABLE_ENTRIES=1024 "$rexglue" codegen usf4_manifest.toml > "$root/out/codegen.log" 2>&1; then
  tail -30 "$root/out/codegen.log"; echo "codegen failed, see out/codegen.log" >&2; exit 1
fi
tail -3 "$root/out/codegen.log"
