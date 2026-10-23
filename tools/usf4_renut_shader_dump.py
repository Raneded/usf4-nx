#!/usr/bin/env python3
"""Collects USF4's Xbox 360 shader containers for Renut's translator (sa2_renderer = "renut", docs/renut-census.md).

Renut's shader cache (app/src/renut_engine/gpu/gpu.cmake) is built from a folder of containers named
{vs,ps}_<xxh3_64 of the container>.bin: the key Renut's XGRegister*Shader hooks look a shader up by. USF4's containers
already exist in two forms, both made from the player's own copy:

  - the ones a run dumped (usf4_dump_shaders, app/src/me/usf4_me_hooks.cpp; out/shaders/in): CreateVertexShader /
    CreatePixelShader's argument, a whole container (virtual part, then physical part);
  - the ones tools/extract_game_shaders.py unpacks from the game files' EMZ streams.

This copies every container of the given folders under Renut's name, once, after the container check of sa2-nx's
tools/sa2_dump_shaders.py (reNut's: sizes, tables and the physical part's offset agree). Containers that fail it are
listed and left out. Containers Renut still lacks are captured at run time (renut_shader_capture_dir) and added by the
next build.

  python3 tools/usf4_renut_shader_dump.py out/shaders/in [more folders] --out out/renut_shader_dump

Read only on its inputs; the output is game data and stays under out/.
"""
import argparse
import csv
import os
import struct
import sys

try:
    import xxhash
except ImportError:
    sys.exit("error: the 'xxhash' module is required (pip install xxhash)")


def check(data):
    """reNut's container check, on a container at offset 0. Returns 'vs', 'ps' or a reason it fails."""
    if len(data) < 0x24:
        return "too short"
    flags, virt, phys = struct.unpack_from(">III", data, 0)
    if flags & 0xFFFFFF00 != 0x102A1100:
        return "not a shader container"
    ct, dt, so = struct.unpack_from(">III", data, 0x10)
    if not (0x24 <= virt < 0x10000 and 0 < phys < 0x100000 and virt + phys == len(data)):
        return "sizes"
    if ct >= virt or dt >= virt or not (0x24 <= so < virt - 8):
        return "tables"
    poff, psize = struct.unpack_from(">II", data, so)
    if poff + psize != phys:
        return "physical part"
    return "ps" if flags & 1 == 0 else "vs"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("inputs", nargs="+", help="folders of .bin containers")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    rows, rejected, written, known = [], [], 0, 0
    for folder in args.inputs:
        for name in sorted(os.listdir(folder)):
            if not name.endswith(".bin"):
                continue
            path = os.path.join(folder, name)
            data = open(path, "rb").read()
            kind = check(data)
            if kind not in ("vs", "ps"):
                rejected.append((path, kind))
                continue
            digest = xxhash.xxh3_64_hexdigest(data)
            target = os.path.join(args.out, f"{kind}_{digest}.bin")
            if os.path.exists(target):
                known += 1
            else:
                with open(target, "wb") as f:
                    f.write(data)
                written += 1
            rows.append((f"{kind}_{digest}.bin", kind, len(data), path))
    with open(os.path.join(args.out, "manifest.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file", "stage", "bytes", "source"])
        w.writerows(rows)
    print(f"{written} containers written, {known} already there, {len(rejected)} rejected -> {args.out}")
    for path, why in rejected:
        print(f"  rejected {path}: {why}")


if __name__ == "__main__":
    main()
