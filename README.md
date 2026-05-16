# TheForceEngineXbox

A fork of [The Force Engine](https://github.com/luciusDXL/TheForceEngine) (luciusDXL's JEDI engine recreation that powers Dark Forces and, eventually, Outlaws) ported to the **original Microsoft Xbox**. Builds to a real `default.xbe` that runs on retail Xbox hardware and CXBX-R.

Upstream is owned and maintained by [@luciusDXL](https://github.com/luciusDXL); this fork exists solely to host the Xbox-specific divergences (XDK toolchain, D3D8 backend, XInput, DirectSound polling, FATX path handling, MSVC 2005 / C++03 patches). All gameplay logic, JEDI engine internals, and asset pipelines are unchanged from upstream.

A purchased copy of **Star Wars: Dark Forces** is required. The original DOS game data files (`DARK.GOB`, `SOUNDS.GOB`, `TEXTURES.GOB`, `SPRITES.GOB`, the `LFD/` folder, etc.) are not redistributed here.

---

## Current state

Boots straight into SECBASE on launch. Controller-driven gameplay loop is functional.

| Working | Partial / Stubbed |
|---|---|
| Software renderer (RClassic_Fixed) at 320×200 → D3D8 → 640×480 | MIDI music (synth wired but iMuse not bridged — silent) |
| 8-bit palette, palette-fx, transparency | No agent menu (boots directly to level 1) |
| Full controller input (XInput) — sticks, D-pad, face buttons, triggers, shoulders | No briefing/cutscenes (Landru bypass on Xbox) |
| Bryar pistol fires, projectiles render, hit effects spawn | No save persistence (virtual in-memory agent only) |
| Enemy AI, projectiles, damage application | No font upscaling assets (`*Num2.fnt` deliberately absent) |
| HUD (health/shields/lives/ammo) | |
| Sound effects + voice (polled DirectSound8 streaming) | |
| TFE_ExternalData fully wired (weapons.json / projectiles.json / effects.json / pickups.json shipped) | |

Output: 640×480 NTSC, 4:3. Pillarboxing not needed — back-buffer is already 4:3.

---

## Building

### Toolchain prerequisites

| Tool | Version | Location expected |
|---|---|---|
| Visual C++ compiler | VC71 (Visual Studio .NET 2003) | `C:\XDK_5558\XDK\xbox\bin\vc71\CL.Exe` |
| XDK | 5558 | `C:\XDK_5558\XDK\xbox\` (includes + libs + `imagebld.exe`) |
| XDK 5849 fallback (for `stdint.h`, `winsock2.h`) | 5849 | `C:\XDK\xbox\include\` |
| Python | 3.x (any modern) | on PATH, for `patchxbe.py` |

The build script is [`build_xbox.bat`](build_xbox.bat) — no `.vcproj`, no MSBuild, no CMake. Direct invocation of `cl.exe` + `link.exe` + `imagebld.exe`. This mirrors OpenJKDF2's working build setup for VC71 + XDK 5558.

### Building from scratch

```cmd
build_xbox.bat clean
build_xbox.bat
```

A clean build compiles ~201 translation units in ~30-60 seconds and produces:

```
build\xbox\release\default.xbe   (~1.1 MB, ready for FTP to Xbox)
build\xbox\release\default.exe   (the PE before patchxbe.py rewrites it)
build\xbox\release\default.xbe.map
build_xbox.log                   (full compiler/linker output)
```

### Deploying to the Xbox

FTP the following to a single directory on the Xbox (typically the `D:\` drive — whatever your dashboard maps as the launch directory for the title):

```
D:\default.xbe                         <- from build\xbox\release\
D:\DARK\DARK.GOB                       <- from your purchased copy
D:\DARK\SOUNDS.GOB
D:\DARK\TEXTURES.GOB
D:\DARK\SPRITES.GOB
D:\DARK\LFD\AGENTMNU.LFD               <- and the rest of LFD\
D:\DARK\LFD\MENU.LFD
D:\DARK\LFD\<...>
D:\DARK\weapons.json                   <- from TheForceEngine\ExternalData\DarkForces\
D:\DARK\projectiles.json
D:\DARK\effects.json
D:\DARK\pickups.json
```

The four JSONs are TFE's externalised game data (weapon stats, projectile physics, hit-effect parameters, pickup definitions). They live next to the GOBs to keep all game data under one directory.

Launch from the dashboard like any other XBE. The XBE's title ID is `LA-001` ("Star Wars Dark Forces") so CXBX-R recognises it correctly.

### Testing on CXBX-R

Same XBE works on [CXBX-R](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded). Useful for fast iteration without FTP cycles. **Real Xbox hardware is the canonical test target** — CXBX-R has HLE quirks (most notably in DSound and XInput) that occasionally diverge from real silicon.

---

## What's different from upstream

All divergence from upstream is constrained to two categories:

1. **Xbox platform code** (always net-new files, lives alongside the originals):
   - `main_xbox.cpp` — XBE entry point
   - `TFE_System/system_xbox.cpp` — timing, logging, NT-prefix file paths
   - `TFE_FileSystem/paths_xbox.cpp` + `fileutil_xbox.cpp` + `filewriterAsync_xbox.cpp`
   - `TFE_Audio/audioDevice_xbox.cpp` — DirectSound8 polled streaming
   - `TFE_Audio/midiPlayer_xbox.cpp` — iMuse stub (TODO: wire fm4Opl3)
   - `TFE_Input/input_xbox.cpp` — XInput polling, synthesised mouse cursor
   - `TFE_RenderBackend/renderBackend_xbox.cpp` + `renderState_xbox.cpp` — D3D8 backend
   - `TFE_RenderShared/texturePacker_xbox.cpp` — CPU-only path
   - `xbox_compat.h` — forced include, defines C++03 shims for `nullptr`/`override`/`final`/`static_assert`
   - `xbox_link_stubs.cpp` — linker stubs for GPU renderer / accessibility / scripting that aren't in the Xbox build
   - `build_xbox.bat`, `patchxbe.py` — toolchain

2. **In-place `#ifdef _XBOX` patches** to game-logic files (e.g. `darkForcesMain.cpp` skips agent menu, `agent.cpp` virtual fallback, `weapon.cpp` ExternalData wire-up). These are kept minimal and clearly bracketed so they diff cleanly against upstream.

Excluded from the Xbox build: `TFE_Editor/`, `TFE_ForceScript/`, `TFE_FrontEndUI/` (ImGui), `TFE_Ui/`, `TFE_PostProcess/`, `TFE_Outlaws/`, `TFE_RenderBackend/Win32OpenGL/`, `TFE_Jedi/Renderer/RClassic_GPU/`, `TFE_Settings/linux/`, `TFE_System/CrashHandler/`.

---

## Roadmap

Near term:
- ~~Wire fm4Opl3 software synth to iMuse → MIDI music~~ — wired in master; tuning in progress
- Save game support via writable HDD partition

Medium term — **port `RClassic_GPU` from OpenGL/GLSL to D3D8 on NV2A**:
The single biggest CPU win available, and the gating dependency for two distinct goals:
- **Audio/synth headroom.** The Xbox CPU is currently running column rasterization, texture sampling, depth interpolation, palette expansion, AI, physics, audio mixing, *and* OPL3 software synthesis. Offloading the rasterizer to the NV2A frees ~30-50% of the CPU budget — enough to run full iMuse music + multiple sound sources without stutter.
- **Split-screen multiplayer.** Original Xbox has 4 controller ports — split-screen is the platform's defining feature. Rendering the world from 2 (co-op) or 4 (deathmatch) viewports per frame is infeasible on the CPU software path but trivial on NV2A (multiple `SetViewport` + draw passes). TFE upstream has no multiplayer at all (Dark Forces was DOS single-player), so this is a genuinely new Xbox-only feature — but the GPU port is the prerequisite.

Not primarily a visual upgrade — at 480p 4:3 the pixels look near-identical to software. It's about CPU headroom and what it unlocks.

Substantial work: TFE's GPU renderer is GLSL 3.3+ with FBOs, MRT, programmable pipeline. NV2A has fixed-function + register combiners (pseudo pixel shaders) + limited vertex programs. A real port, not a mechanical translation. xquake's D3D8 backend is the closest existing reference.

Longer term:
- Front-end menu (level select, options) — non-ImGui, native Xbox UI
- Cutscene playback (Landru cutscene system is in the source tree but disabled)
- Split-screen co-op and vs deathmatch (depends on GPU port)
- System Link / network multiplayer (independent of GPU port but also benefits)
- Outlaws support — pending upstream

---

## Credits

- **JEDI engine reverse-engineering, TFE codebase**: [@luciusDXL](https://github.com/luciusDXL) and the upstream [TheForceEngine](https://github.com/luciusDXL/TheForceEngine) contributors. This fork would not exist without their work.
- **Xbox port**: this fork. Pull requests welcome.
- **Reference Xbox ports** consulted for XDK patterns: OpenJKDF2 (Xbox branch), xquake, Jedi Academy XDK port.
- **Star Wars: Dark Forces** © Disney / LucasArts. The IP belongs solely to Disney. This project does not contain or redistribute any copyrighted game assets.

## License

Same as upstream TFE — see [LICENSE](LICENSE). Xbox-specific patches are released under the same terms.
