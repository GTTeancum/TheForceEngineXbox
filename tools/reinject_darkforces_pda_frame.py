#!/usr/bin/env python3
import argparse
import shutil
import struct
from pathlib import Path

from PIL import Image


def clean_name(raw):
    return raw.split(b"\0", 1)[0].decode("ascii", "replace").strip()


def pad_name(name, size):
    raw = name.encode("ascii")
    if len(raw) > size:
        raw = raw[:size]
    return raw + b"\0" * (size - len(raw))


def read_lfd(path):
    data = Path(path).read_bytes()
    root_type = data[:4].decode("ascii", "replace")
    root_name = clean_name(data[4:12])
    root_len = struct.unpack_from("<I", data, 12)[0]
    count = root_len // 16
    dir_off = 16
    payload_off = 16 + root_len
    entries = []
    for _ in range(count):
        typ = data[dir_off:dir_off + 4].decode("ascii", "replace")
        name = clean_name(data[dir_off + 4:dir_off + 12])
        length = struct.unpack_from("<I", data, dir_off + 12)[0]
        dir_off += 16
        payload = data[payload_off:payload_off + 16 + length]
        p_typ = payload[:4].decode("ascii", "replace")
        p_name = clean_name(payload[4:12])
        p_len = struct.unpack_from("<I", payload, 12)[0]
        entries.append({
            "type": p_typ,
            "name": p_name,
            "length": p_len,
            "data": payload[16:16 + p_len],
        })
        payload_off += 16 + length
    return root_type, root_name, entries


def write_lfd(path, root_type, root_name, entries):
    root_len = len(entries) * 16
    out = bytearray()
    out += pad_name(root_type, 4)
    out += pad_name(root_name, 8)
    out += struct.pack("<I", root_len)
    for entry in entries:
        out += pad_name(entry["type"], 4)
        out += pad_name(entry["name"], 8)
        out += struct.pack("<I", len(entry["data"]))
    for entry in entries:
        out += pad_name(entry["type"], 4)
        out += pad_name(entry["name"], 8)
        out += struct.pack("<I", len(entry["data"]))
        out += entry["data"]
    Path(path).write_bytes(out)


def parse_pltt(data):
    first = data[0]
    last = data[1]
    colors = [(0, 0, 0)] * 256
    off = 2
    for idx in range(first, last + 1):
        colors[idx] = (data[off], data[off + 1], data[off + 2])
        off += 3
    return colors


def nearest_palette_index(rgb, palette):
    r, g, b = rgb
    best = 0
    best_dist = 1 << 62
    for idx, (pr, pg, pb) in enumerate(palette):
        dr = r - pr
        dg = g - pg
        db = b - pb
        dist = dr * dr + dg * dg + db * db
        if dist < best_dist:
            best = idx
            best_dist = dist
            if dist == 0:
                break
    return best


def png_to_indices(png_path, palette):
    img = Image.open(png_path).convert("RGBA")
    if img.size != (320, 200):
        img = img.resize((320, 200), Image.Resampling.NEAREST)
    pixels = bytearray()
    cache = {}
    for r, g, b, a in img.getdata():
        if a == 0:
            pixels.append(0)
            continue
        key = (r, g, b)
        idx = cache.get(key)
        if idx is None:
            idx = nearest_palette_index(key, palette)
            cache[key] = idx
        pixels.append(idx)
    return pixels


def encode_full_delt(pixels, width=320, height=200):
    data = bytearray()
    data += struct.pack("<hhhh", 0, 0, width - 1, height - 1)
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        # Direct, uncompressed line. Low bit clear means literal bytes follow.
        data += struct.pack("<hhh", width << 1, 0, y)
        data += row
    data += struct.pack("<h", 0)
    return bytes(data)


def replace_anim_frame0(anim_data, new_frame_data):
    count = struct.unpack_from("<h", anim_data, 0)[0]
    if count <= 0:
        raise ValueError("pda.ANIM has no frames")
    frames = []
    off = 2
    for i in range(count):
        size = struct.unpack_from("<I", anim_data, off)[0]
        off += 4
        frame = anim_data[off:off + size]
        off += size
        frames.append(frame)
    frames[0] = new_frame_data

    out = bytearray()
    out += struct.pack("<h", count)
    for frame in frames:
        out += struct.pack("<I", len(frame))
        out += frame
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description="Reinject a PNG as MENU.LFD pda.ANIM frame 0.")
    parser.add_argument("--menu-lfd", default=r"C:\Games\Emulators\CXBX\TheForceEngine\DARK\LFD\MENU.LFD")
    parser.add_argument("--png", default="art_extract/darkforces_pda/pda_base_2x_640x400.png")
    parser.add_argument("--out", default=None, help="write to this LFD instead of overwriting --menu-lfd")
    parser.add_argument("--backup", action="store_true", help="create .bak before overwriting --menu-lfd")
    args = parser.parse_args()

    menu_lfd = Path(args.menu_lfd)
    out_path = Path(args.out) if args.out else menu_lfd
    root_type, root_name, entries = read_lfd(menu_lfd)
    pal = next((e for e in entries if e["type"].upper() == "PLTT" and e["name"].lower() == "menu"), None)
    pda = next((e for e in entries if e["type"].upper() == "ANIM" and e["name"].lower() == "pda"), None)
    if not pal or not pda:
        raise RuntimeError("MENU.LFD must contain menu.PLTT and pda.ANIM")

    pixels = png_to_indices(args.png, parse_pltt(pal["data"]))
    new_frame = encode_full_delt(pixels)
    old_len = len(pda["data"])
    pda["data"] = replace_anim_frame0(pda["data"], new_frame)

    if out_path == menu_lfd and args.backup:
        backup = menu_lfd.with_suffix(menu_lfd.suffix + ".bak")
        if not backup.exists():
            shutil.copy2(menu_lfd, backup)
            print(f"backup: {backup}")
        else:
            print(f"backup already exists: {backup}")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    write_lfd(out_path, root_type, root_name, entries)
    print(f"wrote: {out_path}")
    print(f"pda.ANIM length {old_len} -> {len(pda['data'])}")


if __name__ == "__main__":
    main()
