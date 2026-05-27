# Creating an Original Xbox XBE Dashboard Icon

This is the minimum reliable setup for making an original Xbox title show the correct dashboard icon and save metadata. It is game-agnostic and based on retail/XDK behavior.

## Required Files

Create three dashboard metadata files:

- `TitleImage.xbx`: 128x128 title icon, XPR format, usually DXT1. Expected payload size is commonly `0x2800` bytes.
- `SaveImage.xbx`: 64x64 default save icon, XPR format, usually DXT1. Expected payload size is commonly `0x1000` bytes.
- `TitleMeta.xbx`: UTF-16LE text metadata, usually containing at least:

```ini
[default]
TitleName=Your Game Title
```

Keep `TitleMeta.xbx` UTF-16LE with a BOM. The XDK samples and retail titles use this format.

## Build-Time Embedding

When building the XBE, pass all three pieces of metadata to `imagebld`:

```bat
imagebld.exe default.exe ^
  /OUT:default.xbe ^
  /TESTNAME:"Your Game Title" ^
  /TESTID:0x12345678 ^
  /TITLEINFO:titleinfo.txt ^
  /TITLEIMAGE:TitleImage.xbx ^
  /DEFAULTSAVEIMAGE:SaveImage.xbx
```

`/TITLEINFO` creates a `$$XTINFO` section. `/TITLEIMAGE` creates `$$XTIMAGE`. `/DEFAULTSAVEIMAGE` creates `$$XSIMAGE`.

Verify the final XBE with:

```bat
imagebld.exe /DUMP default.xbe
```

Look for:

```text
SECTION HEADER ... $$XTINFO
SECTION HEADER ... $$XTIMAGE
SECTION HEADER ... $$XSIMAGE
Title name: Your Game Title
```

If `$$XTIMAGE` and `$$XSIMAGE` are present but the dashboard still shows the wrong icon, check `$$XTINFO` and runtime metadata installation next.

## Runtime / Installed Metadata

Retail titles and XDK samples also rely on title-level files in the title's user data area:

```text
U:\TitleMeta.xbx
U:\TitleImage.xbx
U:\SaveImage.xbx
```

For a disc-style or homebrew deployment, install or rewrite these files at startup if they are missing or stale. This does not require putting actual game saves in `U:\`; it only gives the dashboard the title metadata it expects.

Recommended startup behavior:

1. Compare each existing `U:\*.xbx` file against the embedded/canonical bytes.
2. If missing or different, write the correct file.
3. Leave normal save data wherever your game expects it.

This is useful because replacement dashboards and save managers may consult the title metadata files rather than only the embedded XBE sections.

## Common Failure Modes

- **Only embedding `$$XTIMAGE`**: Some dashboards still show a fallback or stale icon because `TitleMeta.xbx` / `$$XTINFO` is absent.
- **Wrong text encoding**: `TitleMeta.xbx` should be UTF-16LE, not ASCII or UTF-8.
- **Wrong image format**: `TitleImage.xbx` and `SaveImage.xbx` should be XPR containers, not plain PNG/BMP files renamed to `.xbx`.
- **Wrong dimensions**: Use 128x128 for the title image and 64x64 for the default save image.
- **Dashboard cache confusion**: If metadata changes but the dashboard still shows an old icon, clear the title's cached metadata/save listing or test with a fresh title ID.

## Notes

- The title ID and title name in the XBE certificate should match the intended title identity.
- `TitleImage.xbx` is the dashboard/game icon. `SaveImage.xbx` is the default icon for saves unless the game writes per-save thumbnails.
- `TitleMeta.xbx` and `/TITLEINFO` should use the same title name to avoid dashboard inconsistencies.
