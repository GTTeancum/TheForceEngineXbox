# Xbox Bug Log

## Open Bugs

### XBUG-001: Mission objective menu text is cut off

- Area: UI / mission objective menu
- Status: Fixed
- Notes: The reported cut-off was in the mission briefing/objective panel, not the PDA goals page. The Xbox briefing renderer now uses a wider text clip and a wider scaled destination panel; the earlier PDA-goals tweak was removed.

### XBUG-002: Advanced controls menu needed

- Area: Input / controls UI
- Status: Fixed
- Notes: The Xbox options screens now include remappable controller rows for the current Xbox gameplay actions. The in-game version is a compact datapad-style overlay, uses the cleaner datapad/weapon-wheel font, and keeps controls inside the device panel. Direct individual weapon shortcut rows were removed by request; previous/next weapon remain remappable.

### XBUG-003: Possible cumulative-session freeze near third-level sewer switches

- Area: Runtime stability / level progression
- Status: Addressed - long-play monitoring remains
- Notes: Reported freeze occurred on the third level near the sewer switches after playing straight through from the start. The same area did not freeze after a cold boot and loading a save there. The Xbox level-transition teardown now clears level lifetime resources before starting the next mode, and the runtime now emits focused resource/task/path summaries for long-play diagnosis.

### XBUG-004: Mission Accomplished save prompt alignment

- Area: UI / mission complete
- Status: Fixed
- Notes: The Xbox mission-complete overlay now centers its prompt text from measured glyph width and renders the full `SAVE GAME?` prompt in both 4:3 and 16:9.

### XBUG-005: Default Use/Crouch buttons reversed

- Area: Input / default controls
- Status: Fixed
- Notes: The default Xbox gameplay mapping is now `X` for Use and `B` for Crouch, with old saved swapped mappings migrated automatically.
