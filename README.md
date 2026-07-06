# TheForceEngineXbox

TheForceEngineXbox is an original Xbox port of
[The Force Engine](https://github.com/luciusDXL/TheForceEngine), the open-source
Dark Forces engine recreation.

This fork builds a real `default.xbe` for retail Xbox hardware and CXBX-R. It
does not include Star Wars: Dark Forces. You need your own PC copy of the game.

## Download

Use the latest package from the
[GitHub Releases](https://github.com/GTTeancum/TheForceEngineXbox/releases)
page:

```text
TheForceEngine-Xbox-Install.zip
```

The zip is an overlay package. Copy your PC Dark Forces install to the Xbox
first, then extract this zip over it.

## Install

1. On your Xbox hard drive, make a folder for the game.

   Example:

   ```text
   F:\Games\Dark Forces\
   ```

2. Inside that folder, make a folder named `DARK`.

   Example:

   ```text
   F:\Games\Dark Forces\DARK\
   ```

3. Copy your PC Dark Forces game files into the `DARK` folder.

   The `DARK` folder should contain the normal game data files, such as:

   ```text
   DARK.GOB
   SOUNDS.GOB
   TEXTURES.GOB
   SPRITES.GOB
   LFD\
   ```

4. Optional: if you own the Remastered version and you want the Avenger
   prototype level playable as a mod, copy `extras.gob` into the `DARK` folder
   too.

5. Extract `TheForceEngine-Xbox-Install.zip` into the main game folder, not
   inside `DARK`.

   Example:

   ```text
   F:\Games\Dark Forces\
   ```

6. Let it overwrite files if your file manager asks.

7. Launch `default.xbe`.

## What The Zip Adds

The release package includes the files that are needed for the Xbox port and
are not part of the original PC game:

```text
default.xbe
default_XSIMAGE.xbx
default_XTIMAGE.xbx
TitleMeta.xbx
TitleImage.xbx
SaveImage.xbx
README.txt

DARK\effects.json
DARK\pickups.json
DARK\projectiles.json
DARK\weapons.json

ExternalData\DarkForces\Mods\DF21\df21_catalog_xbox.json
ExternalData\DarkForces\Mods\DF21\thumbs_128x80_rgb565\*.xbt
```

The four JSON files in `DARK` are The Force Engine gameplay data files. They
are required by the Xbox build.

The DF21 catalog and thumbnail cache are included so the mod browser can show
metadata and preview images. The mod levels themselves are not included.

## Mods

This release does not redistribute mods. To install a mod you own or have
downloaded separately, extract it under:

```text
Mods\<mod folder>\
```

The in-game mod browser expects extracted mod folders, not loose zip files in
the game root.

If you own the Remastered version, copying `extras.gob` into `DARK` enables the
Avenger prototype level as a mod entry. `extras.gob` is not included in this
project or in the release zip.

## Current State

This is a 1.0 release-candidate era Xbox build. The focus is hardware stability
and a complete couch-playable Dark Forces experience.

Working in this port:

- Original Xbox `default.xbe` output.
- 640x480 software-rendered gameplay presented through the Xbox D3D8 backend.
- Native Xbox start menu, load menu, options menu, pause/datapad screens, and
  mod browser.
- XInput controller support with adjustable look sensitivity, stick deadzone,
  volume settings, and remappable core actions.
- Mission briefing flow, mission completion flow, and difficulty selection.
- Save/load support using the Xbox title save area, including save thumbnails.
- DirectSound audio path for game audio.
- DF21 metadata and thumbnail support for the mod browser.

Known limitations:

- The hardware world renderer is not enabled for shipping. It remains in the
  tree for future work, but the release build uses the known-good software
  renderer.
- Mods are not bundled. They must be installed separately.
- Star Wars: Dark Forces game data is not bundled and will never be
  redistributed here.

## Build From Source

Most users should download the release zip instead of building from source.

The current command-line build path uses the checked-in `build_xbox.bat` script:

```cmd
build_xbox.bat clean
build_xbox.bat
```

The build script currently expects:

- XDK 5558 at `C:\XDK_5558\XDK\xbox`
- VC71 tools from that XDK
- XDK 5849 headers available at `C:\XDK\xbox\include`
- Python on `PATH`

Successful builds output:

```text
build\xbox\release\default.xbe
```

Always test release builds on real Xbox hardware before treating a change as
shipping-ready. CXBX-R is useful for iteration, but real hardware is the target.

## Credits

- The Force Engine team and contributors, led by
  [luciusDXL](https://github.com/luciusDXL), for The Force Engine.
- Microsoft for Xbox and the Xbox development tools.
- LucasArts for Star Wars: Dark Forces.

## License

Same as upstream The Force Engine. See [LICENSE](LICENSE).

This project does not contain or redistribute copyrighted Star Wars: Dark Forces
game assets.
