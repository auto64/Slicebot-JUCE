# Codex Task Specs — Parity-Driven (Paste-Ready)

**DO NOT BREAK AUDIO**
- Follow `DO_NOT_BREAK_AUDIO.md` constraints.
- Do not change audio routing, device selection, timing, or callback order.
- Do not add allocations on the audio thread.
- Add new functionality in parallel unless explicitly instructed otherwise.

Each task below is **paste-ready**, scoped, and designed to compile independently. Do **not** ask questions when running a task; instead, follow the instructions exactly and limit changes to the listed scope.

---

## T0.1 — Parity Checklist Seed
**Intent**: Create a master parity checklist from Swift audit items.

**Dependencies**: none.

**Scope (allowed files/components)**
- Docs only.
- `PARITY_CHECKLIST.md` (create or update).

**Implementation Steps**
1. Create or update a Markdown checklist mapped to the Swift audit bullets.
2. Ensure each bullet can be checked off independently.
3. Keep items grouped by subsystem (Lifecycle, Source/Cache, Audio, UI, Export, Live, Logging, Tests).

**Files to Touch**
- `PARITY_CHECKLIST.md`

**Do Not Modify**
- Any `.cpp`, `.h`, or UI component files.
- Audio engine code.

**Compile Requirements**
- None (docs-only).

**Acceptance Criteria**
- A single checklist file exists and maps to audit items.
- Checklist is organized by subsystem and ready for tracking.

**Testing Guidance**
- None.

---

## T0.2 — Repo Guardrails
**Intent**: Document non-negotiable audio constraints for every task.

**Dependencies**: none.

**Scope (allowed files/components)**
- Docs only.
- Task header template.

**Implementation Steps**
1. Create a task header template with a **DO NOT BREAK AUDIO** section.
2. Reference `DO_NOT_BREAK_AUDIO.md` explicitly.

**Files to Touch**
- `TASK_HEADER_TEMPLATE.md` (create or update).

**Do Not Modify**
- Any `.cpp`, `.h`, or UI component files.
- Audio engine code.

**Compile Requirements**
- None (docs-only).

**Acceptance Criteria**
- A template exists and includes mandatory audio constraints.

**Testing Guidance**
- None.

---

## T1.1 — Source Picker (Directory)
**Intent**: Implement directory selection (UI + backend hook).

**Dependencies**: T0.1.

**Scope (allowed files/components)**
- UI wiring only (no caching yet).
- Source button handler and state store.

**Implementation Steps**
1. Add a directory chooser dialog tied to the existing “Source” UI button.
2. Store the chosen directory path in state (no persistence yet).
3. Update UI to reflect the selected path (e.g., label).

**Files to Touch**
- Source UI component (e.g., `MainComponent.cpp` / `MainTabView.cpp`).
- State holder (e.g., `SliceStateStore.*`).

**Do Not Modify**
- Audio processing classes.
- Any cache or conversion code.

**Compile Requirements**
- Project builds with the directory picker enabled.

**Acceptance Criteria**
- User can pick a directory.
- Selected path is stored in state and visible in UI.

**Testing Guidance**
- Manual: choose a directory and verify state/UI updates.

---

## T1.2 — Source Picker (Single File)
**Intent**: Implement single-file selection.

**Dependencies**: T0.1.

**Scope (allowed files/components)**
- UI wiring only.

**Implementation Steps**
1. Add a file chooser dialog for single audio file selection.
2. Store selected file path in state (no persistence yet).
3. Update UI to reflect the selected file.

**Files to Touch**
- Source UI component.
- State holder.

**Do Not Modify**
- Audio processing classes.
- Cache/conversion logic.

**Compile Requirements**
- Project builds with the file picker enabled.

**Acceptance Criteria**
- User can pick a file.
- Selected file path is stored in state and visible in UI.

**Testing Guidance**
- Manual: choose a file and verify state/UI updates.

---

## T1.3 — Cache Model + Storage
**Intent**: Implement cache data model and JSON persistence.

**Dependencies**: T1.1.

**Scope (allowed files/components)**
- New cache structs, load/save routines.
- No UI changes required.

**Implementation Steps**
1. Create cache model structs matching required metadata fields.
2. Add JSON serialization/deserialization.
3. Read/write `AudioCache.json` in app support directory.

**Files to Touch**
- New cache model file(s) under `Source/`.
- State store to load/save cache.

**Do Not Modify**
- Audio processing classes.
- UI components beyond basic wiring.

**Compile Requirements**
- Project builds with cache model included.

**Acceptance Criteria**
- `AudioCache.json` can be created and loaded on startup.

**Testing Guidance**
- Manual: run app, trigger save, verify JSON file exists and reloads.

---

## T1.4 — Cache Population
**Intent**: Populate cache by scanning source dir/file.

**Dependencies**: T1.3.

**Scope (allowed files/components)**
- File scanning + metadata extraction.

**Implementation Steps**
1. Scan selected source directory or file.
2. Extract metadata (path, duration, sample rate, channel count).
3. Populate cache entries and persist to JSON.

**Files to Touch**
- Cache population logic under `Source/`.
- State store to update cache.

**Do Not Modify**
- Audio processing classes.
- UI layout.

**Compile Requirements**
- Project builds with cache population logic.

**Acceptance Criteria**
- Cache fills with expected entries after scan.

**Testing Guidance**
- Manual: select source, verify cache entries.

---

## T1.5 — Cache Refresh/Recache Flow
**Intent**: Implement recache logic & progress updates.

**Dependencies**: T1.4.

**Scope (allowed files/components)**
- Cache refresh API, UI status text updates.

**Implementation Steps**
1. Add a recache action (button or menu hook).
2. Implement refresh that clears and repopulates cache.
3. Emit progress updates to status UI.

**Files to Touch**
- Cache manager.
- Status UI component.

**Do Not Modify**
- Audio processing classes.
- Playback engine.

**Compile Requirements**
- Project builds with recache flow.

**Acceptance Criteria**
- Recache action updates cache and status text.

**Testing Guidance**
- Manual: trigger recache and observe status updates.

---

## T2.1 — Conversion Pipeline (Reader)
**Intent**: Convert uncompressed audio into 44.1k/16‑bit/mono.

**Dependencies**: T1.4.

**Scope (allowed files/components)**
- Audio file IO utilities.

**Implementation Steps**
1. Add a conversion routine to resample/downmix/bit-depth convert.
2. Use JUCE audio utilities for conversion (off audio thread).
3. Return converted buffers in internal format.

**Files to Touch**
- `AudioFileIO.*` or equivalent utility file.

**Do Not Modify**
- Audio callback path.
- Device selection.

**Compile Requirements**
- Project builds with conversion helpers.

**Acceptance Criteria**
- Non‑matching files are converted instead of rejected.

**Testing Guidance**
- Manual: load 48k stereo file and verify conversion.

---

## T2.2 — Conversion Integration
**Intent**: Use conversion in slice generation + preview.

**Dependencies**: T2.1.

**Scope (allowed files/components)**
- Slice generation paths.

**Implementation Steps**
1. Route all source reads through conversion pipeline.
2. Ensure converted buffers are used for slicing and preview chain.

**Files to Touch**
- Slice infrastructure/orchestrator.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with conversion integration.

**Acceptance Criteria**
- Non‑matching sources slice successfully.

**Testing Guidance**
- Manual: run slice on 48k stereo file.

---

## T3.1 — Settings Model (BPM/Subdivisions/Samples)
**Intent**: Store user‑configurable slice settings.

**Dependencies**: T1.3.

**Scope (allowed files/components)**
- State store / view model.

**Implementation Steps**
1. Add settings fields (BPM, subdivisions, sample count, transient toggle).
2. Provide defaults matching Swift expectations.

**Files to Touch**
- `SliceStateStore.*` or equivalent.

**Do Not Modify**
- Audio processing classes.

**Compile Requirements**
- Project builds with new settings model.

**Acceptance Criteria**
- Settings exist, mutable, with defaults.

**Testing Guidance**
- Manual: verify defaults and state changes.

---

## T3.2 — Wiring UI to Settings
**Intent**: Connect Main tab controls to settings model.

**Dependencies**: T3.1.

**Scope (allowed files/components)**
- Main tab UI binding.

**Implementation Steps**
1. Wire BPM/subdivision/sample controls to settings state.
2. Update UI to reflect state changes.

**Files to Touch**
- Main tab UI components.

**Do Not Modify**
- Audio processing classes.

**Compile Requirements**
- Project builds with wired controls.

**Acceptance Criteria**
- UI changes update state; state reflects in UI.

**Testing Guidance**
- Manual: adjust controls and verify state.

---

## T3.3 — Slice Generation from Settings
**Intent**: Use settings in slice/reslice/regenerate logic.

**Dependencies**: T3.2, T2.2.

**Scope (allowed files/components)**
- Slice orchestration.

**Implementation Steps**
1. Replace hardcoded slice params with settings.
2. Ensure reslice/regenerate uses updated values.

**Files to Touch**
- `MutationOrchestrator.*`, `SliceInfrastructure.*`.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with new slice logic.

**Acceptance Criteria**
- Changing BPM/subdivisions changes slice lengths.

**Testing Guidance**
- Manual: adjust BPM and verify slice length changes.

---

## T3.4 — Transient Detection Toggle
**Intent**: Enable/disable transient detection.

**Dependencies**: T3.3.

**Scope (allowed files/components)**
- Settings + reslice logic.

**Implementation Steps**
1. Add UI toggle for transient detection.
2. Use toggle to enable/disable refinedStart logic.

**Files to Touch**
- UI and slice infrastructure.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with toggle.

**Acceptance Criteria**
- Toggle changes start frame behavior.

**Testing Guidance**
- Manual: toggle and compare slice starts.

---

## T3.5 — Layering Mode + Merge Modes
**Intent**: Implement layering sample pairing + merge mode UI.

**Dependencies**: T3.3.

**Scope (allowed files/components)**
- State store + preview chain builder.

**Implementation Steps**
1. Add layering/merge mode selection to state + UI.
2. Apply settings in preview chain builder.

**Files to Touch**
- `PreviewChainOrchestrator.*` and UI.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with merge mode wiring.

**Acceptance Criteria**
- Merge mode selection affects chain output.

**Testing Guidance**
- Manual: change modes and verify audio differences.

---

## T4.1 — Preview Chain Playback Control
**Intent**: Add loop playback toggle & one‑shot playback.

**Dependencies**: T3.3.

**Scope (allowed files/components)**
- Playback engine + UI controls.

**Implementation Steps**
1. Add play/stop/loop controls.
2. Route controls to preview chain player.

**Files to Touch**
- Playback controller + UI components.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with playback controls.

**Acceptance Criteria**
- Loop toggle and one‑shot playback work.

**Testing Guidance**
- Manual: play/stop/loop chain.

---

## T4.2 — Per‑Slice Playback
**Intent**: Play individual snippets on command.

**Dependencies**: T4.1.

**Scope (allowed files/components)**
- Preview grid click handlers.

**Implementation Steps**
1. Add per‑slice play action in grid.
2. Route to playback engine to play snippet.

**Files to Touch**
- Grid UI component + playback routing.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with per-slice playback.

**Acceptance Criteria**
- Clicking a slice plays it.

**Testing Guidance**
- Manual: click a slice and listen.

---

## T5.1 — Waveform Rendering (Focus View)
**Intent**: Render waveform of selected slice.

**Dependencies**: T2.2, T3.3.

**Scope (allowed files/components)**
- Focus view rendering.

**Implementation Steps**
1. Read slice buffers into waveform data.
2. Render waveform in focus view component.

**Files to Touch**
- Focus view UI component.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with waveform rendering.

**Acceptance Criteria**
- Waveform visible for selected slice.

**Testing Guidance**
- Manual: select slice and verify waveform.

---

## T5.2 — Preview Grid Rendering
**Intent**: Render per‑slice mini waveforms in grid.

**Dependencies**: T5.1.

**Scope (allowed files/components)**
- Grid view rendering.

**Implementation Steps**
1. Generate waveform thumbnails for each slice.
2. Render thumbnails in grid cells.

**Files to Touch**
- Grid UI component.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with grid waveforms.

**Acceptance Criteria**
- Grid shows waveform thumbnails.

**Testing Guidance**
- Manual: verify thumbnails appear.

---

## T5.3 — Mouse/Modifier Interactions (Grid)
**Intent**: Implement shift/cmd click behavior for grid.

**Dependencies**: T5.2, T4.2.

**Scope (allowed files/components)**
- Input handlers for grid.

**Implementation Steps**
1. Add Shift/Cmd detection to grid mouse events.
2. Implement behaviors: shift‑edit, cmd‑play, hover playhead.

**Files to Touch**
- Grid input handlers.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with modifier handling.

**Acceptance Criteria**
- Grid interactions match Swift expectations.

**Testing Guidance**
- Manual: test Shift/Cmd behavior.

---

## T5.4 — Mouse/Modifier Interactions (Focus View)
**Intent**: Implement focus view modifier behavior.

**Dependencies**: T5.1.

**Scope (allowed files/components)**
- Focus view input handlers.

**Implementation Steps**
1. Add modifier handling for hover playhead.
2. Add stutter highlight behavior.

**Files to Touch**
- Focus view component.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with focus interactions.

**Acceptance Criteria**
- Focus view modifiers match Swift behavior.

**Testing Guidance**
- Manual: verify highlight and playhead.

---

## T6.1 — Stutter Preview Path
**Intent**: Add non‑destructive preview stutter.

**Dependencies**: T3.3.

**Scope (allowed files/components)**
- Stutter pipeline.

**Implementation Steps**
1. Add preview path that writes a temporary stutter file.
2. Ensure original slice is unchanged.

**Files to Touch**
- `MutationOrchestrator.*` / stutter helpers.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with preview stutter.

**Acceptance Criteria**
- Preview is audible without modifying source.

**Testing Guidance**
- Manual: preview stutter and confirm source unchanged.

---

## T6.2 — Stutter Apply + Undo
**Intent**: Apply stutter and allow undo.

**Dependencies**: T6.1.

**Scope (allowed files/components)**
- Stutter pipeline.

**Implementation Steps**
1. Apply stutter to selected slice.
2. Preserve a backup to allow undo.
3. Add undo action hook.

**Files to Touch**
- `MutationOrchestrator.*`.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with undo behavior.

**Acceptance Criteria**
- Undo restores original audio.

**Testing Guidance**
- Manual: apply stutter, undo, verify restore.

---

## T6.3 — Pachinko Stutter Wiring
**Intent**: UI toggle to apply random stutter.

**Dependencies**: T6.2.

**Scope (allowed files/components)**
- UI wiring to existing pachinko stutter logic.

**Implementation Steps**
1. Add UI toggle for pachinko stutter.
2. Wire toggle to existing stutter-all logic.

**Files to Touch**
- UI components.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with pachinko toggle.

**Acceptance Criteria**
- Toggle applies random stutter.

**Testing Guidance**
- Manual: enable toggle, verify stutter.

---

## T6.4 — Pachinko Reverse Wiring
**Intent**: UI toggle for random reverse.

**Dependencies**: T6.2.

**Scope (allowed files/components)**
- UI wiring to existing reverse logic.

**Implementation Steps**
1. Add UI toggle for pachinko reverse.
2. Wire toggle to reverse-all logic.

**Files to Touch**
- UI components.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with reverse toggle.

**Acceptance Criteria**
- Toggle applies random reverse.

**Testing Guidance**
- Manual: enable toggle and verify reverse.

---

## T7.1 — Export Dialog UI
**Intent**: Build export dialog (prefix, checkboxes).

**Dependencies**: T3.3.

**Scope (allowed files/components)**
- UI only.

**Implementation Steps**
1. Build export dialog with prefix input + checkbox options.
2. Wire dialog open/close to export button.

**Files to Touch**
- Export UI components.

**Do Not Modify**
- Export orchestrator logic.

**Compile Requirements**
- Project builds with export dialog.

**Acceptance Criteria**
- Dialog opens and accepts input.

**Testing Guidance**
- Manual: open dialog and edit fields.

---

## T7.2 — Export Preferences Persistence
**Intent**: Save/restore export settings.

**Dependencies**: T7.1.

**Scope (allowed files/components)**
- ApplicationProperties.

**Implementation Steps**
1. Persist export prefs on change.
2. Restore prefs on startup.

**Files to Touch**
- `AppProperties.*` or settings module.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with persisted prefs.

**Acceptance Criteria**
- Export prefs survive restart.

**Testing Guidance**
- Manual: set prefs, restart, verify.

---

## T7.3 — Export Execution Wiring
**Intent**: Connect export dialog to backend.

**Dependencies**: T7.2, T4.1.

**Scope (allowed files/components)**
- Export orchestrator wiring.

**Implementation Steps**
1. Pass dialog settings into export orchestrator.
2. Execute slice + chain exports.

**Files to Touch**
- `ExportOrchestrator.*` + UI glue.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with export wiring.

**Acceptance Criteria**
- Exports generate expected files.

**Testing Guidance**
- Manual: run export and verify outputs.

---

## T8.1 — Recording Format Parity
**Intent**: Change live recording to 16‑bit mono 44.1k.

**Dependencies**: none.

**Scope (allowed files/components)**
- Recording writer.

**Implementation Steps**
1. Update recording writer bit depth to 16‑bit.
2. Ensure format remains mono 44.1k.

**Files to Touch**
- `RecordingWriter.*`.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with updated format.

**Acceptance Criteria**
- WAV headers confirm 16‑bit PCM.

**Testing Guidance**
- Manual: record and inspect file headers.

---

## T8.2 — Recording Storage Path Parity
**Intent**: Store recordings in Application Support/LiveRecordings.

**Dependencies**: T8.1.

**Scope (allowed files/components)**
- File path logic.

**Implementation Steps**
1. Update path to Application Support.
2. Ensure directories are created if missing.

**Files to Touch**
- `RecordingWriter.*` or storage helper.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with updated storage path.

**Acceptance Criteria**
- Recordings appear in Application Support.

**Testing Guidance**
- Manual: record and verify file path.

---

## T8.3 — Live Metadata File
**Intent**: Maintain liveModules.json equivalent.

**Dependencies**: T8.2.

**Scope (allowed files/components)**
- Live recording management.

**Implementation Steps**
1. Create metadata JSON for live modules.
2. Update on record start/stop or module changes.

**Files to Touch**
- Live module management component.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with metadata updates.

**Acceptance Criteria**
- liveModules.json is created and updated.

**Testing Guidance**
- Manual: start/stop recording and inspect JSON.

---

## T9.1 — Multi‑Module Slots
**Intent**: Display multiple live recording modules.

**Dependencies**: T8.2.

**Scope (allowed files/components)**
- Live UI layout.

**Implementation Steps**
1. Add UI layout for multiple module slots.
2. Allow adding/removing slots.

**Files to Touch**
- Live UI components.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with multi-slot layout.

**Acceptance Criteria**
- Multiple modules can be displayed.

**Testing Guidance**
- Manual: add/remove slots.

---

## T9.2 — Module Deletion Behavior
**Intent**: Implement single‑tap clear + double‑tap delete.

**Dependencies**: T9.1.

**Scope (allowed files/components)**
- Live UI interaction logic.

**Implementation Steps**
1. Add single-tap clear action.
2. Add double-tap delete action.

**Files to Touch**
- Live UI components.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with deletion behavior.

**Acceptance Criteria**
- Single tap clears; double tap deletes.

**Testing Guidance**
- Manual: verify clear/delete behavior.

---

## T9.3 — Live UI State Persistence
**Intent**: Persist device + channel selection.

**Dependencies**: T9.1.

**Scope (allowed files/components)**
- ApplicationProperties.

**Implementation Steps**
1. Persist device/channel selections per module.
2. Restore selections on startup.

**Files to Touch**
- Settings persistence modules.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with persistence.

**Acceptance Criteria**
- Device/channel selections persist after restart.

**Testing Guidance**
- Manual: change selection, restart, verify.

---

## T10.1 — Splash Screen
**Intent**: Implement splash UI + timed transition.

**Dependencies**: none.

**Scope (allowed files/components)**
- App startup + UI.

**Implementation Steps**
1. Add splash component with progress indicator.
2. Show splash on launch, then transition after timer.

**Files to Touch**
- App startup (`Main.cpp`) and UI components.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with splash screen.

**Acceptance Criteria**
- Splash appears for ~3 seconds before main UI.

**Testing Guidance**
- Manual: launch app and observe splash.

---

## T10.2 — Startup Cleanup
**Intent**: Delete old live buffers on launch.

**Dependencies**: T8.2.

**Scope (allowed files/components)**
- Startup logic.

**Implementation Steps**
1. On startup, delete files in live recordings directory.
2. Ensure errors are logged but do not block startup.

**Files to Touch**
- App startup logic.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with cleanup logic.

**Acceptance Criteria**
- Live buffer files are removed on launch.

**Testing Guidance**
- Manual: place files, launch app, verify removal.

---

## T11.1 — Shift Shortcuts (SLICE/MOD/JUMBLE/RESLICE/EXPORT)
**Intent**: Implement Swift shortcut keys.

**Dependencies**: T3.3, T7.3.

**Scope (allowed files/components)**
- Input handling.

**Implementation Steps**
1. Add key handlers for Shift+A/M/J/R/E.
2. Route to slice/mod/reslice/export actions.

**Files to Touch**
- Main UI component input handlers.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with shortcuts.

**Acceptance Criteria**
- Shortcuts trigger correct actions.

**Testing Guidance**
- Manual: press keys and verify actions.

---

## T11.2 — Cmd Copy/Paste/Undo
**Intent**: Implement Cmd+C/V/Z for slice copy/paste and stutter undo.

**Dependencies**: T6.2.

**Scope (allowed files/components)**
- Input handling.

**Implementation Steps**
1. Add Cmd+C/V/Z handling.
2. Wire to copy/paste slice and undo stutter.

**Files to Touch**
- Main UI component input handlers.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with copy/paste/undo shortcuts.

**Acceptance Criteria**
- Cmd shortcuts perform expected actions.

**Testing Guidance**
- Manual: trigger shortcuts and verify behavior.

---

## T12.1 — Status Bar Messages
**Intent**: Surface errors/status to UI.

**Dependencies**: T3.3.

**Scope (allowed files/components)**
- Status area updates.

**Implementation Steps**
1. Add a status API to post messages.
2. Surface errors from slice/export/recording operations.

**Files to Touch**
- Status UI component + error reporting hooks.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with status messages.

**Acceptance Criteria**
- Errors appear in status bar.

**Testing Guidance**
- Manual: trigger an error and observe status.

---

## T12.2 — Unified Logging
**Intent**: Standardize logging across audio ops.

**Dependencies**: T12.1.

**Scope (allowed files/components)**
- Logging helpers.

**Implementation Steps**
1. Create a logging helper to standardize log format.
2. Replace ad-hoc logs in audio ops with helper calls.

**Files to Touch**
- Logging utility + audio ops files.

**Do Not Modify**
- Audio callback path.

**Compile Requirements**
- Project builds with logging helper.

**Acceptance Criteria**
- Logs are consistent across operations.

**Testing Guidance**
- Manual: run operations and inspect logs.

