#!/usr/bin/env python3
import argparse
import csv
import struct
from pathlib import Path

from PIL import Image


def psx555_to_rgba(v):
    r = (v & 0x1F) << 3
    g = ((v >> 5) & 0x1F) << 3
    b = ((v >> 10) & 0x1F) << 3
    # Expand the low bits so flat colors do not look slightly too dark.
    r |= r >> 5
    g |= g >> 5
    b |= b >> 5
    return (r, g, b, 255)


def read_vram(path):
    data = path.read_bytes()
    if len(data) % 2:
        raise ValueError(f"{path} has odd byte count")
    width = 1024
    height = len(data) // 2 // width
    if height <= 0 or width * height * 2 != len(data):
        raise ValueError(f"{path} is not a 1024-wide 16-bit VRAM dump")

    img = Image.new("RGBA", (width, height))
    px = img.load()
    off = 0
    for y in range(height):
        for x in range(width):
            (v,) = struct.unpack_from("<H", data, off)
            off += 2
            px[x, y] = psx555_to_rgba(v)
    return img


def read_ctlg(path):
    data = path.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{path} is too small")
    count, clut_count = struct.unpack_from("<II", data, 0)
    expected = 8 + count * 40
    if expected != len(data):
        raise ValueError(f"{path} size mismatch: expected {expected}, got {len(data)}")

    entries = []
    off = 8
    for index in range(count):
        raw_name = data[off:off + 8]
        name = raw_name.split(b"\0", 1)[0].decode("ascii", "replace")
        w, h, tpage, clut, src_u, src_v, dst_x, dst_y = struct.unpack_from("<IIIIIIII", data, off + 8)
        entries.append({
            "index": index,
            "name": name,
            "w": w,
            "h": h,
            "tpage": tpage,
            "clut": clut,
            "src_u": src_u,
            "src_v": src_v,
            "dst_x": dst_x,
            "dst_y": dst_y,
        })
        off += 40
    return count, clut_count, entries


def safe_name(s):
    keep = []
    for ch in s:
        keep.append(ch if ch.isalnum() or ch in ("-", "_") else "_")
    return "".join(keep) or "sprite"


def crop_entry(sheet, entry):
    x = int(entry["src_u"])
    y = int(entry["src_v"])
    w = int(entry["w"])
    h = int(entry["h"])
    return sheet.crop((x, y, x + w, y + h))


def export_pair(ctlg_path, vram_path, out_dir, prefix):
    out_dir.mkdir(parents=True, exist_ok=True)
    sheet = read_vram(vram_path)
    sheet_name = f"{prefix}_sheet.png"
    sheet.save(out_dir / sheet_name)

    count, clut_count, entries = read_ctlg(ctlg_path)
    manifest_path = out_dir / f"{prefix}_manifest.csv"
    with manifest_path.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=[
            "index", "name", "file", "w", "h", "tpage", "clut", "src_u", "src_v", "dst_x", "dst_y"
        ])
        writer.writeheader()
        for entry in entries:
            file_name = f"{prefix}_{entry['index']:03d}_{safe_name(entry['name'])}.png"
            crop_entry(sheet, entry).save(out_dir / file_name)
            row = dict(entry)
            row["file"] = file_name
            writer.writerow(row)

    return {
        "prefix": prefix,
        "count": count,
        "clut_count": clut_count,
        "sheet": sheet_name,
        "manifest": manifest_path.name,
    }


def main():
    parser = argparse.ArgumentParser(description="Extract PS1 Dark Forces PDA CTLG/VRAM art to PNG.")
    parser.add_argument("--root", default="PS1_disc", help="PS1 disc dump root")
    parser.add_argument("--out", default="art_extract/ps1_pda", help="output directory")
    parser.add_argument("--levels", default="1-14", help="levels, e.g. 1-14 or 1,2,9")
    parser.add_argument("--japanese", action="store_true", help="also export JPDACTLG/JPDAVRAM")
    args = parser.parse_args()

    root = Path(args.root)
    out = Path(args.out)
    levels = []
    for part in args.levels.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            levels.extend(range(int(a), int(b) + 1))
        else:
            levels.append(int(part))

    summaries = []
    for level in levels:
        level_dir = root / f"LEV{level}"
        pairs = [("PDACTLG.BIN", "PDAVRAM.BIN", f"LEV{level:02d}_PDA")]
        if args.japanese:
            pairs.append(("JPDACTLG.BIN", "JPDAVRAM.BIN", f"LEV{level:02d}_JPDA"))
        for ctlg_name, vram_name, prefix in pairs:
            ctlg = level_dir / ctlg_name
            vram = level_dir / vram_name
            if not ctlg.exists() or not vram.exists():
                print(f"skip missing {ctlg} / {vram}")
                continue
            summaries.append(export_pair(ctlg, vram, out / f"LEV{level:02d}", prefix))

    index_path = out / "index.csv"
    out.mkdir(parents=True, exist_ok=True)
    with index_path.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=["prefix", "count", "clut_count", "sheet", "manifest"])
        writer.writeheader()
        for row in summaries:
            writer.writerow(row)

    print(f"exported {len(summaries)} catalog(s) to {out}")
    print(f"index: {index_path}")


if __name__ == "__main__":
    main()
