# Handoff: Xbox D3D8 Pause Menu Rendering

## Current status

Sprite/enemy flicker is fixed. Root cause was Xbox-only packed iteration
through sparse sector `objectList` arrays; the renderer now matches
upstream's sparse walk and only increments after a non-null object.

## Current pause-menu symptom

Pressing **START** in-game opens the escape menu logically (audio pauses,
reticle disables, `createRenderTarget` logs). Menu sprites now draw, but
the captured world background is black and the menu layout/texture sampling
is still wrong.

An earlier intermediate state (immediate-mode `screenGPU_blitTextureScaled`,
before the `quadDraw2d_xbox.cpp` port + sprite batching) did render menu
sprites visibly (ABORT MISSION / CONFIGURATION) but with a separate "NO"
confirm button leaking through and no world background. The black-screen
regression came after switching `screenGPU_blitTextureScaled` from
immediate-mode draws to batched queue + flush in `screenGPU_endQuads`.

## Goal

Faithfully port upstream's RClassic_GPU escape menu render path to the
Xbox D3D8 backend. Upstream uses GL shaders + atlas; Xbox uses D3D8
fixed-function. Per the project's hard rule
(`memory/feedback_use_original_source.md`): **adapt from
`TheForceEngine-ORIGINAL/` 1:1, never invent code**. The architecture is
upstream's; only the API layer (shader vs FF) changes.

## Upstream architecture (verified)

Files in `TheForceEngine-ORIGINAL/TheForceEngine/`:

- `TFE_DarkForces/GameUI/escapeMenu.cpp:240-255` — `escapeMenu_open`
  captures the current world frame via `escapeMenu_copyBackground`.
- `escapeMenu.cpp:187-238` — `escapeMenu_copyBackground` GPU branch:
  ```cpp
  if (!s_emState.renderTarget || sizesChanged) {
      s_emState.renderTarget = TFE_RenderBackend::createRenderTarget(w, h);
  }
  TFE_Jedi::endRender();
  TFE_RenderBackend::swap(true);
  TFE_RenderBackend::copyBackbufferToRenderTarget(s_emState.renderTarget);
  TFE_RenderBackend::unbindRenderTarget();
  ```
- `escapeMenu.cpp:314-389` — `escapeMenu_drawGpu` per frame:
  1. `screenGPU_addImageQuad(0, 0, dispW, dispH, getRenderTargetTexture(rt))` — background
  2. `screenGPU_blitTextureScaled(&escMenuFrames[0].texture, ...)` — menu plate
  3. Conditional highlight frames if a button is hovered
  4. Cursor sprite if `drawMouse`
- `TFE_Jedi/Renderer/jediRenderer.cpp:480-498` — every frame:
  ```cpp
  beginRender() { vfb_bindRenderTarget(...); screenDraw_beginQuads(w, h); }
  endRender()   { screenDraw_endQuads(); screenDraw_endLines(); vfb_unbindRenderTarget(); }
  ```
- `TFE_Jedi/Renderer/screenDraw.cpp:52-68` — `screenDraw_beginQuads`
  calls **both** `screenGPU_beginImageQuads` (for `quadDraw2d` —
  background) **and** `screenGPU_beginQuads` (for sprite batch — menu
  sprites). `screenDraw_endQuads` does the matching flushes.
- `TFE_RenderShared/quadDraw2d.cpp` — batched image-quad system used by
  `screenGPU_addImageQuad`. RGBA textures, alpha blending
  `BLEND_ONE / BLEND_INVSRCALPHA`.
- `TFE_Jedi/Renderer/RClassic_GPU/screenDrawGPU.cpp` — sprite batch
  (`s_scrQuads[SCR_MAX_QUAD_COUNT*4]`) flushed in `screenGPU_endQuads`
  with one `drawIndexedTriangles` per draw group. Uses
  `gpu_render_quad.vert/.frag` shader with palette indirection.
- `TFE_DarkForces/mission.cpp:563-702` — mission task wraps each frame in
  `beginRender()` / `endRender()`. **`escapeMenu_open` is called from
  inside this wrap**, meaning the mid-frame `endRender + swap` in
  `escapeMenu_copyBackground` happens BEFORE the mission's normal
  end-of-frame `endRender` at line 691. The mission's end-of-frame
  `endRender` therefore runs against a *closed* scene with empty batches
  — relies on backend guards.

## What's been added (uncommitted on `master`)

| File | Change |
|---|---|
| `TheForceEngine/TFE_RenderShared/quadDraw2d_xbox.cpp` | NEW. Xbox port of upstream `quadDraw2d.cpp`. Same API; batch is just per-quad calls to `gpuDrawScreenQuad` (no real D3D8 VB batching yet). |
| `TheForceEngine/TFE_RenderBackend/renderBackend_xbox.h` | Added `gpuDrawScreenQuad(x0,y0,x1,y1,u0,v0,u1,v1, vdispW, vdispH, tex, alphaTest, topColor=white, botColor=white)`. |
| `TheForceEngine/TFE_RenderBackend/renderBackend_xbox.cpp` | Real `createRenderTarget` / `freeRenderTarget` / `copyBackbufferToRenderTarget` / `getRenderTargetTexture` / `getRenderTargetDim`. `D3DSWAPEFFECT_COPY` (was `_DISCARD`). RT registry (`s_xboxRTRegistry[8]`) instead of sentinel disambiguation. `gpuDrawScreenQuad` — `D3DFVF_XYZRHW | DIFFUSE | TEX1`, `MODULATE` on color, `SELECTARG1` on alpha (DIFFUSE alpha for RT path, TEXTURE alpha for sprite/DELT path), alpha blend OFF, alpha test only on sprite path. |
| `TheForceEngine/xbox_link_stubs.cpp` | `screenGPU_init/destroy` → `quadInit/Destroy`. `screenGPU_beginImageQuads/endImageQuads` → `quadDraw2d_begin/draw`. `screenGPU_addImageQuad` → `quadDraw2d_add` (verbatim port of upstream `screenDrawGPU.cpp:278-289`). `screenGPU_blitTexture/blitTextureScaled` now **queue** into `s_scrSpriteQuads[1024]`; `screenGPU_endQuads` flushes via per-quad `gpuDrawScreenQuad`. Diagnostic logs in `endQuads` + `quadDraw2d_draw` (fire on count-change, won't spam). |
| `build_xbox.bat` | Added `TheForceEngine\TFE_RenderShared\quadDraw2d_xbox.cpp` to source list. |

Build is clean (202/202). Latest XBE produced at `build/xbox/release/default.xbe`.

## What's known good

- Logical menu state opens correctly (audio pause, reticle off,
  `createRenderTarget` runs and logs a non-NULL handle).
- World renderer, HUD overlay, input — all unaffected and working.
- `IADF_MENU_TOGGLE` binding to `CONTROLLER_BUTTON_START` on Xbox.

## What's likely wrong (debugging targets, in priority order)

### 1. Back-buffer capture format mismatch (most likely)

`pp.BackBufferFormat = D3DFMT_X8R8G8B8` (token value 0x07, swizzled).
Xbox D3D8 spec says render targets cannot be swizzled, so the driver
treats this as effectively linear. My RT uses `D3DFMT_LIN_X8R8G8B8`
(0x1E). `CopyRects` may still silently fail/no-op on this format pair.

**Action:**
- The diagnostic log on `copyBackbufferToRenderTarget` (just added) will
  print the `CopyRects` HRESULT. Boot, press START, read the log.
- If HRESULT is non-zero or shows 0x00000000 but the captured surface is
  still black, try matching the back buffer format token exactly
  (`D3DFMT_X8R8G8B8` on the RT) and see if Xbox D3D8 auto-promotes it
  to linear because of `D3DUSAGE_RENDERTARGET`.
- Cross-reference with `OpenJKDF2_Xbox` (sibling reference port in
  `C:\Programming\GitHub`) — does it do `CopyRects` from back buffer to
  a texture anywhere? If so copy the exact format pair it uses.

### 2. `screenGPU_endQuads` may not be firing visibly

After the immediate-mode → batched switch, no on-screen menu sprites.
The diagnostic logs (just added) will say `screenGPU_endQuads flushing N
sprite quads` and `quadDraw2d_draw flushing N image quads (M groups)`
on each count change. If `N == 0` the batches aren't being filled →
`escapeMenu_drawGpu` isn't running OR is failing internally. If
`N > 0` but screen is still black, the `gpuDrawScreenQuad` calls aren't
producing visible output.

**Action:**
- Read the log on next test. Confirm whether sprite/image batches are
  actually populated each frame the menu is open.

### 3. `blitVdispQuad` overlay may be covering the menu

`swap(true)` in `renderBackend_xbox.cpp:527-559` calls
`blitVdispQuad(alphaTest=true)` *after* the menu's draws but *before*
`EndScene`. `blitVdispQuad` blits `s_vdispTex` (the software 8-bit
framebuffer texture) on top of whatever we drew. When paused, the
software framebuffer retains the **last pre-pause HUD frame** (weapon,
ammo display, etc.) because nothing clears it. With alpha test, only
palette-0 pixels are discarded — every other pixel of the stale HUD
draws over the menu.

**Action:**
- Try skipping `blitVdispQuad` when escape menu is open. Either pass a
  flag through `swap()` or clear `s_vdispTex` when the menu opens. The
  cleanest path is probably to clear the CPU framebuffer
  (`vfb_getCpuBuffer()`) in `escapeMenu_open` on the Xbox side so the
  next `vfb_swap`-uploaded texture is all-transparent.

### 4. Double `endRender` per frame on menu-open frame

`escapeMenu_copyBackground` calls `endRender + swap` mid-frame.
`mission.cpp:691` calls `endRender` again at end of frame. The second
call runs through `screenDraw_endQuads` → my flushes — which call
`gpuDrawScreenQuad` with `s_gpuSceneOpen == false`. The early-return
guard makes that a no-op so it shouldn't cause harm, but it means the
opening frame can never queue+flush from the same context. Not a
black-screen cause on its own but worth tracking.

### 5. UV / pow2 padding off

DELT sprite textures aren't pow2 (e.g. 256x122). My
`gpuGetOrUploadIndexedTexture` pads to pow2 (256x128). `scrGpu_uvMax`
in `xbox_link_stubs.cpp` computes `srcW/padW`, `srcH/padH` and passes
those as `u1`, `v1`. If this math is wrong the sprite samples padding
(garbage) and may produce black or wrong-orientation output. Verify by
testing with a known pow2 texture.

## Suggested debugging sequence

1. **Boot the existing build with diagnostic logging.** Read the log
   after pressing START. Three things should appear:
   - `copyBackbufferToRenderTarget hr=0x... rt=... back=...` (HRESULT)
   - `quadDraw2d_draw flushing N image quads (M groups, vdisp WxH)` (N=1 expected)
   - `screenGPU_endQuads flushing N sprite quads (vdisp WxH)` (N=1-4 expected)
2. **If `copyBackbufferToRenderTarget` HRESULT is non-zero** — fix RT format match (item 1 above).
3. **If `N == 0` for either batch** — `escapeMenu_drawGpu` isn't being entered or doesn't reach the blits. Add a `TFE_System::logWrite` at the top of `escapeMenu_drawGpu` (it's in shared `escapeMenu.cpp`, just gate with `#ifdef _XBOX`) to confirm it runs.
4. **If both `N > 0` and HRESULT==0 but screen still black** — `blitVdispQuad` is the culprit (item 3 above). Try forcing `blitVirtualDisplay=false` in `swap()` when escape menu is open.

## File layout reference

- Project: `C:\Programming\GitHub\TheForceEngine-master\`
- Xbox build script: `build_xbox.bat` (VS2003 + XDK 5558)
- Output: `build\xbox\release\default.xbe`
- Logs viewed via CXBX-R OutputDebugStringA or real Xbox XBDM
- Upstream reference: `TheForceEngine-ORIGINAL\` (PC TFE, untouched)
- Sibling Xbox reference: `OpenJKDF2_Xbox\` (working Xbox D3D8 port of
  OpenJKDF2 — see `memory/reference_xbox_ports.md`)

## Commit policy

Per `memory/feedback_commit_means_push.md` — when user says "commit",
commit AND `git push origin master` in the same turn. Do NOT push
speculative fixes; verify on hardware first. Per
`memory/feedback_use_original_source.md` — port from upstream verbatim,
never invent.

## Honest assessment of where I got stuck

I iterated through multiple speculative fixes (sentinel pointer
disambiguation, alpha blending mode, swap effect, RT format,
batching) without grounding each in measured signal. The user
correctly identified this as wasted time. The blocker is **lack of
visibility into the actual D3D state when the menu is open** — the
diagnostic logs added in the latest uncommitted change are the
prerequisite for any further work. Run them first; let the data drive
the next change.
