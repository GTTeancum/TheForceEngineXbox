# TheForceEngine Xbox Port — Claude Code Handoff

## Project Goal

Port **The Force Engine** (TFE, a Dark Forces remake built on the JEDI engine) to the original Xbox using VS2005 + XDK 5849. Target: produce a working `.xbe` testable in CXBX-R. Stretch goal: Xbox 360 BC via `xefu`.

---

## Build Environment

| Item | Value |
|---|---|
| Compiler | MSVC 8.00 (Visual Studio 2005) |
| SDK | XDK 5849 installed at `C:\XDK` (non-standard — not `$(XEDK)`) |
| Platform tag in vcproj | `Win32` (NOT `Xbox` — XDK registers as Win32 in VS2005) |
| Config names | `Debug\|Win32` and `Release\|Win32` |
| Include dirs | `C:\XDK\xbox\include;C:\XDK\xbox\include\DirectX;.` |
| Lib dir | `C:\XDK\lib` |
| Release libs | `d3d8-xbox.lib xboxkrnl.lib xgraphics.lib xonline.lib xacteng.lib xnet.lib xapilib.lib` |
| Debug libs | `d3d8-xbox.lib xboxkrnl.lib xgraphicsd.lib xonlined.lib xactengd.lib xnetd.lib xapilibd.lib xbdm.lib` |
| Forced include | `xbox_compat.h` (via `ForcedIncludeFiles` in VCCLCompilerTool) |
| Post-build | `python patchxbe.py "$(OutDir)\default.exe"` |
| C files | `CompileAs="1"`, C++ files `CompileAs="2"` |
| Disable warnings | `4996;4244;4267;4305` |
| Release extras | `/FIXED:NO` AdditionalOptions, `IgnoreAllDefaultLibraries="true"`, `FixedBaseAddress="2"` |
| Encoding | `Windows-1252` |

**Critical:** CXBX-R only has HLE signatures for retail (Release) libs. Always test Release builds.

**Critical:** The vcproj uses Platform `Win32` — matching the OpenJKDF2_Xbox.vcproj reference. Do NOT use `Xbox` as the platform name.

---

## Project File

`TheForceEngine\TheForceEngine_Xbox.vcproj` — VS2005 `.vcproj` format (NOT `.vcxproj`).  
Open directly via **File → Open → Project/Solution**. Do not use `TheForceEngine.sln` (that is VS2017 format and will fail).

---

## Source Layout

All source lives under `TheForceEngine\`. The `.vcproj` references files relative to its own location. Xbox-specific replacement files should be placed alongside the originals they replace.

### Forced Include: `xbox_compat.h`

Lives at `TheForceEngine\xbox_compat.h`. Applied to every translation unit. It:
- Includes `<xtl.h>` and `<xgmath.h>` first (matching `CoreXboxCompat.h` pattern from existing XDK projects)
- Kills colliding XDK macros: `max`, `min`, `MAKEFOURCC`, `Top`
- Defines C++03 shims: `#define nullptr NULL`, `#define override`, `#define final`, `#define static_assert(e,m)`, `#define noexcept throw()`, `#define constexpr const`
- Sets `#pragma conform(forScope, off)`
- Defines `_XBOX` if not already defined
- Defines `ENABLE_EDITOR 0`

---

## Rendering Architecture

- **Software renderer only** — `RClassic_Fixed` (preferred for perf) and `RClassic_Float`
- 8-bit paletted framebuffer CPU-expanded to XRGB8888
- Blitted via D3D8 `UpdateSurface` to 1280×720 back buffer with 4:3 pillarbox (160px each side)
- `TFE_RenderBackend::swap()` calls `Present()`
- All GPU renderer code (`RClassic_GPU/`, `Win32OpenGL/`, `TFE_PostProcess/`, etc.) is excluded

## Audio Architecture

- **DirectSound8** double-buffered (2× `HALF_BUFFER_BYTES` = 2× 8192 bytes)
- Notify thread fires `TFE_AudioCallback` each half-buffer
- MIDI → baked OGG (iMuse MIDI messages are no-ops via stub)
- No SDL anywhere

## Input

- XInput polling via `TFE_InputXbox::pollInput()` each frame
- Feeds existing `TFE_Input` state arrays unchanged
- Start+Back = quit

## File System / Paths

- All relative to XBE launch directory
- `DARK\` subdirectory for game data (mirrors original Dark Forces install)
- `Saves\` for user data and log output
- No SDL, no posix paths

---

## Files Delivered (all relative to `TheForceEngine\`)

### New Xbox-specific files

| File | Notes |
|---|---|
| `xbox_compat.h` | Forced include — C++03 shims + XDK setup |
| `main_xbox.cpp` | Entry point replaces `main.cpp` |
| `TFE_System/system_xbox.cpp` | Replaces `system.cpp` + `log.cpp` |
| `TFE_FileSystem/paths_xbox.cpp` | Replaces `paths.cpp` + `paths-posix.cpp` |
| `TFE_FileSystem/fileutil_xbox.cpp` | Replaces `fileutil.cpp` |
| `TFE_FileSystem/filewriterAsync_xbox.cpp` | Replaces `filewriterAsync.cpp` (synchronous WriteFile) |
| `TFE_Audio/audioDevice_xbox.cpp` | Replaces `audioDevice.cpp` (DirectSound8 notify thread) |
| `TFE_Audio/midiPlayer_xbox.cpp` | Replaces `midiPlayer.cpp` + `RtMidi.cpp` + `systemMidiDevice.cpp` |
| `TFE_Input/input_xbox.cpp` + `input_xbox.h` | XInput polling |
| `TFE_Input/inputMapping.cpp` | Replaces original (STL vectors→fixed arrays, SDL removed) |
| `TFE_Input/replay_xbox.cpp` | Replaces `replay.cpp` |
| `TFE_Game/reticle_xbox.cpp` | Replaces `reticle.cpp` |
| `TFE_RenderBackend/renderBackend_xbox.cpp` | Replaces entire `Win32OpenGL/` render backend |
| `TFE_RenderBackend/renderState_xbox.cpp` | Replaces `Win32OpenGL/renderState.cpp` |
| `TFE_RenderShared/texturePacker_xbox.cpp` | Replaces `texturePacker.cpp` (CPU palette only, no GPU) |

### Replaced/patched headers (shared PC+Xbox, guarded by `#ifdef _XBOX`)

| File | Notes |
|---|---|
| `TFE_System/types.h` | Atomic types → `CRITICAL_SECTION`-based stubs |
| `TFE_System/profiler.h` | Disables profiler (`TFE_PROFILE_ENABLED` not set when `_XBOX`) |
| `TFE_System/parser.h` + `parser.cpp` | `TokenList` → `XboxString` fixed array |
| `TFE_FileSystem/fileutil.h` | `FileList` → fixed array with `XboxString` proxy |
| `TFE_FileSystem/filewriterAsync.h` | `nullptr` default → `NULL` |
| `TFE_Audio/audioOutput.h` | `OutputDeviceInfo`: `std::string` → `char[256]` |
| `TFE_Audio/audioDevice.h` | `SDL_AudioCallback` → `TFE_AudioCallback` typedef |
| `TFE_Audio/audioSystem.cpp` | SDL_mutex → CRITICAL_SECTION; `std::min/max` → inline; `nullptr` → NULL |
| `TFE_FrontEndUI/frontEndUi.h` | Full stub; `AppState` enum redeclared |
| `TFE_FrontEndUI/console.h` | `CVar` stub; `getCVarCount()` returns 0 |
| `TFE_ForceScript/forceScript.h` | All inline no-ops |
| `TFE_A11y/accessibility.h` | Stubs + `toLower(std::string)` for `settings.cpp` |
| `TFE_RenderBackend/dynamicTexture.h` | Removes `std::vector` member |
| `TFE_RenderShared/texturePacker.h` | C++03 constructors; no in-class initializers |
| `TFE_Settings/settings.h` | In-class initializers → constructors; mod structs guarded |
| `TFE_Settings/settings.cpp` | `nullptr`→NULL; mod functions guarded `#ifndef _XBOX`; `writeCVars` guarded |
| `TFE_Input/replay.h` | All inline stubs |

### Patched game `.cpp` files (aggregate `= {}` → memset, struct constructors)

All replacements are drop-in for the originals at the same path:

- `TFE_DarkForces/darkForcesMain.cpp` — `RunGameState`/`SharedGameState` constructors; `= {}` → memset
- `TFE_DarkForces/mission.cpp` — `lumMaskGpu`/`palFxGpu` struct assignment; `min()` clamp inline
- `TFE_DarkForces/player.cpp` — `s_playerLogic.move/dir = {}` → memset
- `TFE_DarkForces/sound.cpp` — `sound_state = {}` → memset
- `TFE_DarkForces/weaponFireFunc.cpp` — `mine->vel = {0,0,0}` → explicit field assignment
- `TFE_DarkForces/GameUI/escapeMenu.cpp` — `EscapeMenuState` constructor; `= {}` → memset
- `TFE_DarkForces/Landru/ldraw.cpp` — `ldraw_state = {}` → memset
- `TFE_DarkForces/Landru/cutscene_film.cpp` — `s_filmState = {}` → memset
- `TFE_DarkForces/Actor/bobaFett.cpp` — `s_shared = {}` → memset
- `TFE_DarkForces/Actor/dragon.cpp` — `s_shared = {}` → memset
- `TFE_DarkForces/Actor/mousebot.cpp` — `s_mouseBotRes = {}` → memset
- `TFE_DarkForces/Actor/phaseOne.cpp` — `s_shared = {}` → memset
- `TFE_DarkForces/Actor/phaseTwo.cpp` — `s_shared = {}` → memset
- `TFE_DarkForces/Actor/phaseThree.cpp` — `s_shared = {}` → memset
- `TFE_DarkForces/Actor/turret.cpp` — `s_turretRes = {}` → memset
- `TFE_DarkForces/Actor/welder.cpp` — `s_shared = {}` → memset
- `TFE_Jedi/Level/robjData.cpp` — `s_objData = {}` → memset
- `TFE_Jedi/Level/rtexture.cpp` — `s_texState = {}` → memset
- `TFE_Polygon/polygon.cpp` — `std::isfinite` → `_finite`

---

## Excluded from Xbox Build (set `ExcludedFromBuild="true"` in vcproj or omit entirely)

### Entire directories excluded

- `TFE_Editor/` — editor tooling
- `TFE_ForceScript/` — AngelScript engine
- `TFE_DarkForces/Scripting/` — game scripting
- `TFE_FrontEndUI/` — ImGui console/UI (stub headers provided)
- `TFE_Ui/` — Dear ImGui
- `TFE_PostProcess/` — OpenGL post-processing
- `TFE_Outlaws/` — Outlaws game (not porting)
- `TFE_A11y/accessibility.cpp` — accessibility (stub header provided)
- `TFE_RenderBackend/Win32OpenGL/` — OpenGL backend
- `TFE_Jedi/Renderer/RClassic_GPU/` — GPU renderer
- `TFE_Settings/linux/` — Linux Steam path detection
- `TFE_System/CrashHandler/` — Windows crash handler

### Individual files excluded (replaced by Xbox equivalents)

| Excluded | Replaced by |
|---|---|
| `main.cpp` | `main_xbox.cpp` |
| `TFE_System/system.cpp` | `system_xbox.cpp` |
| `TFE_System/log.cpp` | (merged into `system_xbox.cpp`) |
| `TFE_System/profiler.cpp` | (macros are no-ops via `profiler.h`) |
| `TFE_FileSystem/paths.cpp` | `paths_xbox.cpp` |
| `TFE_FileSystem/paths-posix.cpp` | `paths_xbox.cpp` |
| `TFE_FileSystem/fileutil.cpp` | `fileutil_xbox.cpp` |
| `TFE_FileSystem/fileutil-posix.cpp` | `fileutil_xbox.cpp` |
| `TFE_FileSystem/filestream-posix.cpp` | (not needed) |
| `TFE_FileSystem/filewriterAsync.cpp` | `filewriterAsync_xbox.cpp` |
| `TFE_Audio/audioDevice.cpp` | `audioDevice_xbox.cpp` |
| `TFE_Audio/midiPlayer.cpp` | `midiPlayer_xbox.cpp` |
| `TFE_Audio/RtMidi.cpp` | `midiPlayer_xbox.cpp` |
| `TFE_Audio/systemMidiDevice.cpp` | `midiPlayer_xbox.cpp` |
| `TFE_Input/inputMapping.cpp` (original) | `inputMapping.cpp` (patched) |
| `TFE_Input/replay.cpp` | `replay_xbox.cpp` |
| `TFE_Game/reticle.cpp` | `reticle_xbox.cpp` |
| `TFE_RenderShared/texturePacker.cpp` | `texturePacker_xbox.cpp` |
| `TFE_RenderShared/lineDraw2d.cpp` | (GPU only, excluded) |
| `TFE_RenderShared/lineDraw3d.cpp` | (GPU only, excluded) |
| `TFE_RenderShared/triDraw2d.cpp` | (GPU only, excluded) |
| `TFE_RenderShared/triDraw3d.cpp` | (GPU only, excluded) |
| `TFE_RenderShared/quadDraw2d.cpp` | (GPU only, excluded) |
| `TFE_RenderShared/modelDraw.cpp` | (GPU only, excluded) |
| `TFE_RenderShared/lineDrawMode.cpp` | (GPU only, excluded) |

---

## C++03 Compatibility Strategy

MSVC 8.00 (VS2005) is C++03 only. Key rules applied throughout:

1. **`nullptr`** → `#define nullptr NULL` in `xbox_compat.h` (forced include)
2. **`override` / `final`** → `#define override` / `#define final` in `xbox_compat.h`
3. **`static_assert`** → `#define static_assert(e,m)` in `xbox_compat.h`
4. **`constexpr`** → `#define constexpr const` in `xbox_compat.h`
5. **`noexcept`** → `#define noexcept throw()` in `xbox_compat.h`
6. **In-class non-static member initializers** (`int x = 0;` in structs) — integral types tolerated by MSVC `/Ze` extension; float types must use constructors
7. **Aggregate assignment** (`var = {};`) → `memset(&var, 0, sizeof(var));`
8. **Aggregate assignment with values** (`vec = {x,y,z};`) → explicit field assignment
9. **`std::min` / `std::max`** → inline ternary `(a < b ? a : b)`
10. **`std::isfinite`** → `_finite` (MSVC 2005 CRT)
11. **`auto` keyword** → not needed (none found in included files)
12. **Range-based for** → not needed (none found in included files)
13. **Lambdas** → not needed (none found in included files)
14. **`std::string` / `std::vector`** as struct members → replace with `char[]` / fixed arrays in Xbox-facing headers; leave untouched in `.cpp` bodies (MSVC 2005 STL is fine)

---

## Known Pending Issues / Next Steps

### Not yet audited

- `TFE_Audio/MidiSynth/fm4Opl3Device.cpp` and `soundFontDevice.cpp` — included in build, not yet checked for STL issues. May need patches.
- `TFE_Settings/windows/registry.cpp` — included in build, not yet checked for C++11 issues.
- `TFE_Asset/imageAsset.cpp` — has `catch(std::exception&)` which is fine in MSVC 2005.

### First-build error triage

When errors appear, fix them in batches by category:
- **C2143 / C2059** syntax errors → likely `nullptr`, `override`, or aggregate init missed by the forced include
- **C2065** undeclared identifier → missing stub or wrong include order
- **LNK2019** unresolved external → missing stub `.cpp` or wrong lib
- **C4819** character encoding → safe to ignore with `/wd4819`

### Runtime / XBE

- `patchxbe.py` must be in the solution root (one level above `TheForceEngine\`)
- It patches subsystem 1→14, runs `C:\XDK\xbox\bin\imagebld.exe`, injects D3D8/XGRAPHC version entries for CXBX-R HLE
- CXBX-R requires Release build for HLE detection
- Game data: place Dark Forces install in `DARK\` relative to the XBE

### Save / log location

- `TFE_System/system_xbox.cpp` writes logs to `Saves\tfe_log.txt` relative to XBE launch dir
- On CXBX-R the launch dir is wherever the XBE is

---

## Reference Files

- `OpenJKDF2_Xbox.vcproj` — the working reference vcproj showing correct VS2005+XDK format. Key facts confirmed from it:
  - Platform = `Win32` (not `Xbox`)
  - Configs = `Debug|Win32` / `Release|Win32`
  - `Keyword="Win32Proj"`
  - `CharacterSet="2"`
  - All tool stubs present (VCCustomBuildTool, VCXMLDataGeneratorTool, etc.)
  - `encoding="Windows-1252"`
  - `CompileAs="1"` globally for C project; TFE needs `"2"` for C++ with `.c` files overriding to `"1"`
  - `IgnoreAllDefaultLibraries="true"` in Release
  - `FixedBaseAddress="2"` in Release
  - `/FIXED:NO` in Release linker AdditionalOptions
