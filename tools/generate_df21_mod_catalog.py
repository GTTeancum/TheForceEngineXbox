#!/usr/bin/env python
"""Generate the Xbox DF-21 mod catalog and exact-size thumbnails.

Input is the DF-21 owner-provided JSON at the repository root. Output is a
runtime-focused catalog plus RGB565 thumbnail files that the Xbox code can load
without PNG decoding or image scaling.
"""

from __future__ import print_function

import argparse
import json
import os
import posixpath
import struct
import sys
import tempfile
import urllib.request

try:
    from PIL import Image
except ImportError:
    Image = None


CATALOG_VERSION = 1
THUMB_MAGIC = b"XBT1"
THUMB_FORMAT_RGB565 = 1


def repo_path(*parts):
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", *parts))


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def normalize_date(value):
    if not value:
        return ""
    parts = str(value).split("-")
    if len(parts) != 3:
        return str(value)
    try:
        year = int(parts[0])
        month = int(parts[1])
        day = int(parts[2])
    except ValueError:
        return str(value)
    if month < 1 or month > 12:
        return str(value)
    if day < 1:
        day = 1
    if day > 31:
        return str(value)
    return "%04d-%02d-%02d" % (year, month, day)


def as_bool(value):
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() == "true"


def trim_text(value):
    if value is None:
        return ""
    return " ".join(str(value).replace("\r", " ").replace("\n", " ").split())


def local_mod_dir(filename):
    base = os.path.splitext(os.path.basename(filename))[0]
    return "D:\\mods\\%s" % base


def download(url, out_path):
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "TheForceEngine-Xbox-CatalogPrep/1.0",
            "Accept": "image/png,image/*;q=0.8,*/*;q=0.5",
        },
    )
    fd, tmp_path = tempfile.mkstemp(prefix="df21_", suffix=".download")
    os.close(fd)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            with open(tmp_path, "wb") as out_file:
                while True:
                    chunk = response.read(65536)
                    if not chunk:
                        break
                    out_file.write(chunk)
        os.replace(tmp_path, out_path)
    except Exception:
        if os.path.exists(tmp_path):
            os.unlink(tmp_path)
        raise


def write_xbt(image_path, out_path, width, height):
    if Image is None:
        raise RuntimeError("Pillow is required. Install with: py -m pip install Pillow")

    img = Image.open(image_path).convert("RGB")
    src_w, src_h = img.size
    target_ratio = float(width) / float(height)
    src_ratio = float(src_w) / float(src_h)

    if abs(src_ratio - target_ratio) > 0.001:
        if src_ratio > target_ratio:
            crop_w = int(src_h * target_ratio)
            left = (src_w - crop_w) // 2
            img = img.crop((left, 0, left + crop_w, src_h))
        else:
            crop_h = int(src_w / target_ratio)
            top = (src_h - crop_h) // 2
            img = img.crop((0, top, src_w, top + crop_h))

    if img.size != (width, height):
        img = img.resize((width, height), Image.LANCZOS)

    pixels = img.load()
    ensure_dir(os.path.dirname(out_path))
    with open(out_path, "wb") as out_file:
        out_file.write(THUMB_MAGIC)
        out_file.write(struct.pack("<HHHH", CATALOG_VERSION, width, height, THUMB_FORMAT_RGB565))
        for y in range(height):
            row = bytearray()
            for x in range(width):
                r, g, b = pixels[x, y]
                value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                row.extend(struct.pack("<H", value))
            out_file.write(row)


def make_entry(level, thumb_rel_path):
    filename = trim_text(level.get("filename"))
    filepath = trim_text(level.get("filepath"))
    slug = trim_text(level.get("levelname"))
    current_version_key = ""
    base = os.path.splitext(filename)[0]
    prefix = slug + "_"
    if base.startswith(prefix):
        current_version_key = base[len(prefix):]
    elif base == slug:
        current_version_key = "default"

    return {
        "slug": slug,
        "title": trim_text(level.get("name")),
        "author": trim_text(level.get("author")),
        "rating": int(level.get("rating") or 0),
        "description": trim_text(level.get("description")),
        "createDate": normalize_date(level.get("create_date")),
        "lastModDate": normalize_date(level.get("last_mod_date")),
        "source": "df-21",
        "sourceLevelUrl": "https://df-21.net/downloads/levels/%s/" % slug,
        "coverUrl": trim_text(level.get("cover")),
        "thumbnail": thumb_rel_path.replace("\\", "/"),
        "currentVersion": {
            "key": current_version_key,
            "filename": filename,
            "url": filepath,
            "installDir": local_mod_dir(filename),
        },
        "support": {
            "dos": as_bool(level.get("dos_support")),
            "remaster": as_bool(level.get("remaster_support")),
        },
        "links": {
            "review": trim_text(level.get("review") or level.get("review ")),
            "walkthrough": trim_text(level.get("walkthrough")),
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default=repo_path("df_level_list.json"))
    parser.add_argument("--output-dir", default=repo_path("TheForceEngine", "ExternalData", "DarkForces", "Mods", "DF21"))
    parser.add_argument("--source-cache-dir", default=repo_path("art_extract", "df21_source_covers"))
    parser.add_argument("--width", type=int, default=128)
    parser.add_argument("--height", type=int, default=80)
    parser.add_argument("--skip-download", action="store_true")
    args = parser.parse_args()

    if Image is None:
        print("Pillow is required. Install with: py -m pip install Pillow", file=sys.stderr)
        return 2

    with open(args.input, "r", encoding="utf-8") as in_file:
        source = json.load(in_file)

    levels = source.get("levels", [])
    output_dir = os.path.abspath(args.output_dir)
    source_cover_dir = os.path.abspath(args.source_cache_dir)
    thumb_dir = os.path.join(output_dir, "thumbs_%dx%d_rgb565" % (args.width, args.height))
    ensure_dir(source_cover_dir)
    ensure_dir(thumb_dir)

    entries = []
    failures = []
    for index, level in enumerate(levels, 1):
        slug = trim_text(level.get("levelname"))
        cover_url = trim_text(level.get("cover"))
        cover_ext = posixpath.splitext(posixpath.basename(cover_url))[1] or ".png"
        cover_path = os.path.join(source_cover_dir, slug + cover_ext)
        thumb_name = slug + ".xbt"
        thumb_path = os.path.join(thumb_dir, thumb_name)
        thumb_rel_path = os.path.relpath(thumb_path, output_dir)

        try:
            if not args.skip_download and (not os.path.exists(cover_path) or os.path.getsize(cover_path) == 0):
                print("[%03d/%03d] download %s" % (index, len(levels), slug))
                download(cover_url, cover_path)
            if not os.path.exists(thumb_path):
                print("[%03d/%03d] convert  %s" % (index, len(levels), slug))
                write_xbt(cover_path, thumb_path, args.width, args.height)
            entries.append(make_entry(level, thumb_rel_path))
        except Exception as exc:
            failures.append({"slug": slug, "cover": cover_url, "error": str(exc)})
            print("[%03d/%03d] FAILED   %s: %s" % (index, len(levels), slug, exc), file=sys.stderr)

    entries.sort(key=lambda item: item["title"].lower())
    catalog = {
        "catalogVersion": CATALOG_VERSION,
        "source": "df-21",
        "sourceJson": os.path.basename(args.input),
        "thumbnail": {
            "format": "XBT1_RGB565_LE",
            "width": args.width,
            "height": args.height,
            "bytesPerImage": args.width * args.height * 2 + 12,
        },
        "install": {
            "mode": "extracted_zip_basename",
            "root": "D:\\mods",
            "example": "D:\\mods\\academy_modern\\",
        },
        "levelCount": len(entries),
        "levels": entries,
    }

    ensure_dir(output_dir)
    catalog_path = os.path.join(output_dir, "df21_catalog_xbox.json")
    with open(catalog_path, "w", encoding="utf-8", newline="\n") as out_file:
        json.dump(catalog, out_file, indent=2, ensure_ascii=False)
        out_file.write("\n")

    if failures:
        failure_path = os.path.join(output_dir, "thumbnail_failures.json")
        with open(failure_path, "w", encoding="utf-8", newline="\n") as out_file:
            json.dump(failures, out_file, indent=2)
            out_file.write("\n")

    print("Wrote %s" % catalog_path)
    print("Wrote %d thumbnails to %s" % (len(entries), thumb_dir))
    if failures:
        print("%d failures; see thumbnail_failures.json" % len(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
