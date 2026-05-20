#!/usr/bin/env python3
import argparse
import csv
import struct
from pathlib import Path

from PIL import Image


def clean_name(raw):
    return raw.split(b"\0", 1)[0].decode("ascii", "replace").strip()


def safe_name(s):
    return "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in s) or "asset"


def read_lfd(path):
    data = Path(path).read_bytes()
    if len(data) < 16:
        raise ValueError(f"{path} is too small")
    root_len = struct.unpack_from("<I", data, 12)[0]
    count = root_len // 16
    dir_off = 16
    payload_off = 16 + root_len
    entries = []
    for index in range(count):
        typ = clean_name(data[dir_off:dir_off + 4])
        name = clean_name(data[dir_off + 4:dir_off + 12])
        length = struct.unpack_from("<I", data, dir_off + 12)[0]
        dir_off += 16
        entries.append({
            "index": index,
            "type": typ,
            "name": name,
            "length": length,
            "offset": payload_off + 16,
            "data": data[payload_off + 16:payload_off + 16 + length],
        })
        payload_off += 16 + length
    return entries


def parse_pltt(data):
    if len(data) < 2:
        raise ValueError("palette too small")
    first = data[0]
    last = data[1]
    colors = [(0, 0, 0, 255)] * 256
    off = 2
    for idx in range(first, last + 1):
        if off + 3 > len(data):
            break
        colors[idx] = (data[off], data[off + 1], data[off + 2], 255)
        off += 3
    return colors


def decode_delt(data):
    if len(data) < 8:
        raise ValueError("DELT too small")
    left, top, right, bottom = struct.unpack_from("<hhhh", data, 0)
    width = right - left + 1
    height = bottom - top + 1
    if width <= 0 or height <= 0 or width > 4096 or height > 4096:
        raise ValueError(f"bad DELT bounds {left},{top},{right},{bottom}")

    pixels = bytearray(width * height)
    off = 8
    while off + 6 <= len(data):
        size_and_type, x_start, y_start = struct.unpack_from("<hhh", data, off)
        off += 6
        if size_and_type == 0:
            break
        rle = (size_and_type & 1) != 0
        pixel_count = (size_and_type >> 1) & 0x3FFF
        dst = (y_start - top) * width + (x_start - left)
        if rle:
            remaining = pixel_count
            while remaining > 0 and off < len(data):
                code = data[off]
                off += 1
                count = code >> 1
                if count <= 0:
                    continue
                if (code & 1) == 0:
                    run = data[off:off + count]
                    off += count
                else:
                    if off >= len(data):
                        break
                    run = bytes([data[off]]) * count
                    off += 1
                end = min(dst + len(run), len(pixels))
                if dst < len(pixels):
                    pixels[dst:end] = run[:max(0, end - dst)]
                dst += count
                remaining -= count
        else:
            run = data[off:off + pixel_count]
            off += pixel_count
            end = min(dst + len(run), len(pixels))
            if dst < len(pixels):
                pixels[dst:end] = run[:max(0, end - dst)]
    return {
        "left": left,
        "top": top,
        "right": right,
        "bottom": bottom,
        "width": width,
        "height": height,
        "pixels": pixels,
    }


def indexed_to_image(frame, palette, transparent):
    img = Image.new("RGBA", (frame["width"], frame["height"]))
    out = []
    for idx in frame["pixels"]:
        r, g, b, _ = palette[idx]
        a = 0 if transparent and idx == 0 else 255
        out.append((r, g, b, a))
    img.putdata(out)
    return img


def parse_anim(data):
    if len(data) < 2:
        raise ValueError("ANIM too small")
    count = struct.unpack_from("<h", data, 0)[0]
    off = 2
    frames = []
    for frame_index in range(count):
        if off + 4 > len(data):
            break
        size = struct.unpack_from("<I", data, off)[0]
        off += 4
        frame_data = data[off:off + size]
        off += size
        frames.append((frame_index, frame_data))
    return frames


def find_entry(entries, typ, name):
    typ_l = typ.lower()
    name_l = name.lower()
    for entry in entries:
        if entry["type"].lower() == typ_l and entry["name"].lower() == name_l:
            return entry
    return None


def write_frame(out_dir, prefix, frame_index, frame, palette):
    base = f"{prefix}_{frame_index:03d}_{frame['width']}x{frame['height']}_xy{frame['left']}_{frame['top']}"
    transparent_path = out_dir / f"{base}_transparent.png"
    opaque_path = out_dir / f"{base}_opaque.png"
    indexed_to_image(frame, palette, True).save(transparent_path)
    indexed_to_image(frame, palette, False).save(opaque_path)
    return transparent_path.name, opaque_path.name


def export_anim(entry, out_dir, palette):
    rows = []
    frames = parse_anim(entry["data"])
    for frame_index, frame_data in frames:
        if not frame_data:
            continue
        frame = decode_delt(frame_data)
        trans, opaque = write_frame(out_dir, safe_name(entry["name"]), frame_index, frame, palette)
        rows.append({
            "asset": entry["name"],
            "type": entry["type"],
            "frame": frame_index,
            "transparent_png": trans,
            "opaque_png": opaque,
            "x": frame["left"],
            "y": frame["top"],
            "w": frame["width"],
            "h": frame["height"],
        })
    return rows


def export_delt(entry, out_dir, palette):
    frame = decode_delt(entry["data"])
    trans, opaque = write_frame(out_dir, safe_name(entry["name"]), 0, frame, palette)
    return [{
        "asset": entry["name"],
        "type": entry["type"],
        "frame": 0,
        "transparent_png": trans,
        "opaque_png": opaque,
        "x": frame["left"],
        "y": frame["top"],
        "w": frame["width"],
        "h": frame["height"],
    }]


def main():
    parser = argparse.ArgumentParser(description="Extract Dark Forces LFD DELT/ANIM art to PNG.")
    parser.add_argument("--menu-lfd", default=r"C:\Games\Emulators\CXBX\TheForceEngine\DARK\LFD\MENU.LFD")
    parser.add_argument("--dfbrief-lfd", default=r"C:\Games\Emulators\CXBX\TheForceEngine\DARK\LFD\DFBRIEF.LFD")
    parser.add_argument("--out", default="art_extract/darkforces_pda")
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    menu_entries = read_lfd(args.menu_lfd)
    pal_entry = find_entry(menu_entries, "PLTT", "menu")
    if not pal_entry:
        raise RuntimeError("MENU.LFD does not contain menu.PLTT")
    palette = parse_pltt(pal_entry["data"])

    rows = []
    menu_out = out / "menu_lfd"
    menu_out.mkdir(parents=True, exist_ok=True)
    for name in ("pda", "cursor"):
        entry = find_entry(menu_entries, "ANIM", name) or find_entry(menu_entries, "DELT", name)
        if not entry:
            continue
        rows.extend(export_anim(entry, menu_out, palette) if entry["type"].upper() == "ANIM" else export_delt(entry, menu_out, palette))

    brief_entries = read_lfd(args.dfbrief_lfd)
    brief_out = out / "dfbrief_lfd"
    brief_out.mkdir(parents=True, exist_ok=True)
    for entry in brief_entries:
        if entry["type"].upper() in ("ANIM", "DELT"):
            rows.extend(export_anim(entry, brief_out, palette) if entry["type"].upper() == "ANIM" else export_delt(entry, brief_out, palette))

    manifest = out / "manifest.csv"
    with manifest.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=["asset", "type", "frame", "transparent_png", "opaque_png", "x", "y", "w", "h"])
        writer.writeheader()
        for row in rows:
            writer.writerow(row)

    print(f"exported {len(rows)} frame(s) to {out}")
    print(f"manifest: {manifest}")


if __name__ == "__main__":
    main()
