# Xbox Hardware Resource Audit

This port should be treated like a console title, not a desktop app that happens
to boot on Xbox. The current code is functional but still has several desktop-era
resource habits: global archive/search-path state, scattered heap allocations,
full-frame CPU uploads, and transition-time logging. CXBX-R tolerated many of
these. Retail hardware exposes the weak seams through audio stutter, mod swap
freezes, and save/load edge cases.

The guiding rule from the retail Xbox references is simple: every resource must
belong to a named lifetime, and every transition must return the process to a
known-good state.

## Retail Patterns Worth Copying

### XQuake

XQuake is the closest philosophical match: old PC engine, much newer console.
Its memory model is explicit:

- persistent startup allocations live below a host hunk mark;
- level/session allocations are made after the mark;
- transition cleanup is `Hunk_FreeToLowMark(host_hunklevel)`;
- cache allocations are disposable and can be flushed/compacted.

TFE's `MemoryRegion` is conceptually close, but `region_clear()` keeps all
blocks resident. For Xbox we already improved `freeGame()` to destroy/recreate
the game and level regions. The next step is to formalize marks/pools for
frontend, level, mod, cutscene, and temporary decode work.

### Mercenaries

The Mercenaries source uses named systems, small-block pools, counted managers,
asset queues, never-unload lists, and explicit cancel/update/release calls. The
important lesson is not the exact API, but the ownership model: assets are not
"whatever the file system can currently find." They are owned by systems and
groups.

TFE should grow a small Xbox resource coordinator that owns:

- mounted archives/search paths;
- frontend textures/fonts/audio;
- current level resources;
- current mod package;
- transient decode buffers;
- save-thumbnail buffers.

### Re-Volt / Jade

These engines lean on prepared Xbox resources and batched rendering. They avoid
per-item immediate work when a reusable resource can exist in GPU memory. TFE's
gameplay renderer is intentionally software, but the frontend and UI overlays
should still avoid unnecessary CPU churn: static art should be cached, dirty
rects should be possible, and full-screen texture uploads should be measured.

## Current TFE Resource Domains

### Memory

Current behavior:

- `MemoryRegion` grows by malloc-backed blocks.
- Xbox game/level regions use 1 MB blocks.
- `region_clear()` preserves high-water blocks.
- `freeGame()` now destroys/recreates the game and level regions on Xbox.

Risks:

- transient file loads still use raw `malloc`, `new`, STL containers, and temp
  buffers outside named regions;
- no global memory budget or peak tracking exists;
- repeated mod transitions can fragment the heap even when game/level regions
  are clean.

Target:

- add named Xbox pools/regions: `frontend`, `level`, `mod`, `cutscene`,
  `scratch`, and `save`;
- add lightweight peak counters and transition snapshots;
- enforce "no unknown owner" for large allocations.

### Archives And Search Paths

Current behavior:

- `paths_xbox.cpp` stores global vectors of search paths, file mappings, and
  mounted archives.
- `clearLocalArchives()` frees mounted archives.
- `removeFirstArchive()` / `removeLastArchive()` only pop pointers.
- `Archive::getArchive()` caches archives globally by path.

Risks:

- ownership is ambiguous: some archives are cached, some are temporary, some are
  only popped;
- file mappings can point to mod-local LFDs after a mod transition if cleanup is
  incomplete;
- a cached archive can survive longer than the mod/session that introduced it.

Work already done:

- `Archive::freeArchive()` now frees by pointer, not by `archive->m_name`;
- `freeGame()` now clears archive state during launch preparation.

Target:

- introduce scoped mount groups: base game, frontend, active mod, active level;
- forbid anonymous archive mounts on Xbox;
- add one `resetForFrontend()` and one `resetForGameLaunch()` path that leaves
  no stale mod state.

### Mods

Current behavior:

- Xbox mod menu scans extracted folders under `D:\Mods\<modname>\`.
- It finds loose `.GOB` payloads and optional metadata.
- Older runtime ZIP support still existed in `loadCustomGob()`.

Risk:

- live ZIP mounting loads compressed containers into memory, may inflate nested
  GOBs into heap memory, and writes temporary LFDs to `Temp\`.

Work done in this pass:

- Xbox now refuses ZIP/PK3/GOBX runtime mounting. Users must extract mods to
  folders containing the GOB.

Target:

- delete or hard-disable the old `Temp\` path for Xbox entirely;
- require one GOB per active mod;
- silo mod quicksaves by stable mod id/hash;
- when leaving a mod, explicitly unload the mod mount group and all external
  JSON/logics/pickup/projectile/weapon overrides.

### Level Loading

Current behavior:

- levels and related geometry are loaded through the normal TFE path;
- many resources end up in level/game memory regions, but asset managers also
  cache textures, sprites, VOCs, palettes, fonts, GMID, VUE, etc.

Risks:

- "free game" is doing too much because level shutdown is not a single owner;
- a failed load can leave a partially initialized resource set;
- mod-to-mod load after gameplay is the highest-risk path.

Target:

- add a two-phase level load:
  1. prepare into scratch/level-staging;
  2. commit active level only after all required assets are valid;
- on failure, return to frontend with a clean base mount state;
- record load-phase logs only at phase boundaries.

### Landru / FMV / Cutscenes

Current behavior:

- Landru/cutscene resources use their own memory region plus file/archive
  lookups.
- Cutscenes remain 320x200 by design.

Risks:

- cutscene cleanup and game/frontend cleanup are separate flows;
- menu music and cutscene audio can overlap or fight on transition if state
  changes are interleaved.

Target:

- make cutscene playback a named phase with a dedicated mount and audio mode;
- return from cutscene to a known frontend/game phase, never to an implicit
  leftover state.

### Audio / MIDI

Current behavior:

- DirectSound pump runs on a thread and calls the TFE audio callback.
- The audio callback mixes sound and MIDI.
- MIDI has its own update thread and locks around callback/device state.
- Transition paths call pause/resume/volume/stop while the menu/game state is
  changing.

Risks:

- single CPU, several locks, and transition-time log writes can create audible
  stalls;
- audio callback currently does too much work under lock;
- menu transitions may restart or adjust music more often than necessary.

Work done in this pass:

- disabled Xbox audio-path logging in `audioSystem.cpp`.

Target:

- make menu selection changes zero-audio-work unless the selected action really
  changes tracks;
- move MIDI command processing to a predictable frame boundary or use a
  lock-free/single-writer command ring;
- never call log formatting from audio or MIDI hot paths in retail builds;
- profile worst-case MIDI render cost, then decide whether baked/native audio is
  worth it.

### Rendering / UI

Current behavior:

- gameplay is software-rendered, palette-expanded on CPU, then uploaded to D3D8.
- native Xbox UI overlays draw into the expanded buffer.
- several menus now run at 640x480, while Landru/cutscenes remain 320x200.

Risks:

- full-frame CPU expand/upload every frame is acceptable for gameplay today, but
  leaves limited headroom for richer UI or 720p;
- static menu screens still redraw more than a console frontend normally would;
- UI state is mixed with render-backend globals instead of a frontend resource
  owner.

Target:

- cache static frontend/menu layers as textures;
- use dirty updates for menus where only selection changes;
- keep gameplay framebuffer upload separate from UI composition;
- evaluate moving simple primitives/sprites/text to GPU for menus while keeping
  core gameplay software-rendered.

### Saves / Logs / Settings

Current behavior:

- saves and logs currently live under the launch directory (`D:\Saves\...`) on
  Xbox because UDATA attempts broke boot.
- save thumbnails are embedded as raw 160x90 ARGB.
- logs are much quieter than early builds, but some save/load logs are still
  chatty.

Risks:

- writes during gameplay can stall if synchronous and large;
- D:\ may behave differently between HDD launch, softmod virtual drive, and
  disc-like paths;
- settings.ini is still a mutable desktop-style config.

Target:

- keep `D:\Saves` until UDATA can be reintroduced behind a proven title
  manifest/save flow;
- write saves atomically: temp file, flush, rename;
- throttle logs and keep only transition/resource snapshots in retail testing;
- bake Xbox-critical settings into the XBE and ignore unsafe settings.ini keys.

## Priority Fix Plan

1. Stabilize resource phase boundaries.
   - Frontend enter/exit.
   - Game launch.
   - Level load/unload.
   - Mod mount/unmount.
   - Cutscene enter/exit.

2. Remove desktop-only runtime paths from Xbox.
   - No live ZIP/PK3/GOBX mount.
   - No Temp\ extraction.
   - No dynamic settings that alter renderer/audio/platform invariants.

3. Add resource snapshots.
   - Free memory if available.
   - Game/level region block count and used bytes.
   - Archive/search-path/file-mapping counts.
   - Active mod id/path.
   - Audio state.

4. Make audio real-time safe.
   - No logging from hot paths.
   - Fewer transition commands.
   - Smaller lock spans.
   - Measure MIDI render cost before choosing baked audio.

5. Move frontend/UI toward console ownership.
   - Static textures and cached layers.
   - Dirty updates.
   - Explicit unload when leaving frontend.

6. Revisit UDATA only after resource stability.
   - Use retail examples for title ID/icon/save metadata.
   - Keep it behind a compile-time switch until it passes hardware boot,
     save, load, delete, and dashboard tests.

## Current Hardware-Hardening Pass

This pass moves several risky desktop-style paths behind stricter Xbox
boundaries:

- archive cache ownership is now pointer-safe, so cached archives can be
  removed by the actual object being unmounted instead of relying on mismatched
  name/path keys;
- resource snapshots now log free memory, search path count, mounted archive
  count, file mapping count, cached archive count, and game/level memory-region
  usage at launch, load, mod-menu, and return-to-frontend boundaries;
- returning from gameplay to the frontend now performs an explicit runtime
  purge of search paths, local archives, cached archives, external JSON
  overrides, asset caches, and relative input state;
- launching or loading a game now starts from the same purge path before adding
  the target mod/game search path;
- Xbox refuses runtime ZIP/PK3/GOBX mounting and requires extracted
  `Mods\<modname>\` folders with a loose GOB, avoiding resident ZIP buffers,
  nested GOB inflation, and `Temp\` LFD extraction;
- noisy path/audio/menu logs are gated so the next hardware log has transition
  signal instead of hot-path chatter.

## Definition Of Done

- Start menu -> stock level -> main menu -> mod A -> main menu -> mod B works
  repeatedly on hardware.
- Mod quicksave/resume works per mod without affecting campaign quicksave.
- Audio does not stutter on menu selection, screen transitions, or level load.
- Every major transition logs one resource snapshot, not a stream of hot-path
  chatter.
- Repeated 30-minute stress run does not show rising memory or stale archive
  counts.
- CXBX-R is no longer the authority; hardware is.
