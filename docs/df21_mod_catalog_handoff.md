# DF-21 Mod Catalog Xbox Handoff

## Goal

Add a Dark Forces custom level browser that uses the owner-provided DF-21 JSON as the canonical catalog source. The Xbox build should ship with the current catalog and preprocessed thumbnails, then install mods as extracted folders instead of running levels from ZIP files.

## Generated Assets

The generated runtime assets live at:

```text
TheForceEngine/ExternalData/DarkForces/Mods/DF21/df21_catalog_xbox.json
TheForceEngine/ExternalData/DarkForces/Mods/DF21/thumbs_128x80_rgb565/*.xbt
```

Source cover PNGs were downloaded only as a build/prep cache:

```text
art_extract/df21_source_covers/*.png
```

Do not package the source PNG cache into the Xbox build.

## Generator

The prep script is:

```text
tools/generate_df21_mod_catalog.py
```

Default run:

```text
py -3 tools/generate_df21_mod_catalog.py
```

It reads:

```text
df_level_list.json
```

It writes a slim catalog and exact-size thumbnails. The current generated thumbnail size is `128x80`, preserving the source `320x200` aspect ratio without runtime scaling.

If the final UI needs a different thumbnail size, rerun with:

```text
py -3 tools/generate_df21_mod_catalog.py --width 160 --height 100
```

Then update the game-code thumbnail folder path to match the generated directory name.

## Catalog Rules

Use `levelname` from the source JSON as the stable slug. In the generated catalog this is:

```json
"slug": "academy"
```

Only the preferred/current ZIP is used for deployment:

```json
"currentVersion": {
  "key": "modern",
  "filename": "academy_modern.zip",
  "url": "https://df-21.net/downloads/levels/academy/academy_modern.zip",
  "installDir": "D:\\mods\\academy_modern"
}
```

Ignore older `versions` for the first Xbox implementation. The owner JSON has version labels/dates, but not explicit ZIP URLs for every older version. The generated catalog intentionally keeps only `filename` and `filepath`.

## Install Layout

Xbox hardware was getting bogged down when running mods inside ZIPs. Install by extracting the downloaded ZIP into a folder named from the ZIP basename:

```text
D:\mods\academy_modern.zip     old/not preferred
D:\mods\academy_modern\[files] new/preferred
```

Use the generated catalog's `currentVersion.installDir` as the canonical install path.

Recommended installed-state record:

```json
{
  "slug": "academy",
  "filename": "academy_modern.zip",
  "installDir": "D:\\mods\\academy_modern",
  "installedAt": "2026-05-26",
  "status": "installed"
}
```

Keep installed state separate from `df21_catalog_xbox.json` so catalog refreshes do not disturb user installs.

## XBT Thumbnail Format

Each `.xbt` file is fixed-size and exact-display-size.

For `128x80`:

```text
header: 12 bytes
pixels: 128 * 80 * 2 = 20480 bytes
total: 20492 bytes
```

Header layout, little-endian:

```text
bytes 0..3   magic: "XBT1"
u16          version: 1
u16          width
u16          height
u16          format: 1 = RGB565 little-endian
```

Pixel data is row-major RGB565 little-endian. Upload to D3D8 as a 16-bit texture/surface. Do not scale these thumbnails in the UI; lay out the UI around the generated size.

Memory target:

```text
one 128x80 RGB565 thumbnail: ~20 KB
226 thumbnails on disk: ~4.63 MB
24-thumbnail runtime cache: ~480 KB plus texture overhead
```

Use an LRU cache keyed by `slug`. Keep only visible rows plus a small scroll buffer in memory.

## Runtime Loading

Startup/load order:

1. Read bundled `df21_catalog_xbox.json`.
2. Overlay cached update catalog from `Saves\ModCache\DF21\df21_catalog_xbox.json` if present.
3. Read installed-state file from `Saves\ModCache\DF21\installed_mods.json` or similar.
4. Populate the menu from catalog entries.

For each visible level row:

1. Get `entry.thumbnail`.
2. Load the `.xbt` only when the row enters the visible/cache window.
3. Release the least recently used thumbnail when the cache limit is reached.

For new runtime-discovered levels without preprocessed thumbnails, show a placeholder until runtime image conversion exists.

## Runtime Refresh

The first implementation can skip live refresh and rely entirely on bundled data.

When adding refresh later, prefer fetching an updated JSON manifest rather than scraping HTML. Compare by `slug`:

- new slug: add catalog entry, placeholder thumbnail until generated/downloaded
- existing slug with changed `currentVersion.filename`: mark update available
- missing slug: keep entry but optionally mark unavailable

If direct DF-21 runtime refresh is required, the Xbox needs HTTPS via a small TLS client such as mbedTLS. DF-21 redirects HTTP to HTTPS.

## Current Output Verification

Generated on 2026-05-26:

```text
catalog entries: 226
thumbnail count: 226
thumbnail size: 128x80 RGB565
bad thumbnail sizes: 0
runtime thumbnail bytes: 4,631,192
catalog bytes: 300,120
```

Known source-data cleanup handled by the generator:

- `review ` with trailing whitespace is normalized into `links.review`.
- Date strings with single-digit or zero days are normalized when safe, e.g. day `0` becomes `01`.
- Description/title/author strings are compacted to single-line text.

