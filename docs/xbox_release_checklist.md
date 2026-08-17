# Xbox Release Checklist

Use this checklist for every public TheForceEngineXbox release.

## Required Assets

- [ ] Build with `build_xbox.bat` and confirm it succeeds.
- [ ] Confirm `build\xbox\release\default.xbe` exists.
- [ ] Confirm `build\xbox\release\box art.png` exists.
- [ ] Include `box art.png` at the root of the install ZIP beside `default.xbe`.
- [ ] Upload the same `box art.png` with the GitHub release assets.
- [ ] Do not rename, recompress, or omit `box art.png`.

The build intentionally fails when the repository-root `box art.png` is
missing. This keeps the required release asset from being silently dropped.

## Verification

- [ ] Verify the install ZIP contains every file listed under "What The Zip
  Adds" in the root README.
- [ ] Verify the package contains no copyrighted Dark Forces game data.
- [ ] Smoke-test dashboard-selected 4:3 and 16:9 on XEMU using the same ISO.
- [ ] Confirm the dashboard's 480p flag controls progressive scan independently
  of aspect ratio when the XEMU EEPROM and AV-pack configuration support it.
- [ ] Confirm no `tfe_force_480p.txt` or `tfe_force_720p.txt` override is present
  or recognized.
- [ ] Test the final release build on original Xbox hardware.
- [ ] Confirm the README cover and screenshots render on GitHub.
