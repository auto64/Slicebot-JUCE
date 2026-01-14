# Parity Checklist — Swift Audit → JUCE Tasks

Use this checklist to track parity completion against the Swift audit. Each item maps to a Swift behavior gap or partial match from the audit summary.

## A. App Lifecycle / Splash
- [ ] Splash screen + timed transition before main UI.
- [ ] Startup cleanup of live recording buffers.
- [ ] Window size matches Swift (620×615).

## B. Source Selection + Security + Cache
- [ ] Directory picker wired to state.
- [ ] Single file picker wired to state.
- [ ] Security-scoped bookmark or platform-equivalent persistence.
- [ ] AudioCache.json read/write support.
- [ ] Recache flow + progress updates.

## C. Slice Generation & Settings
- [ ] BPM/subdivision/sample settings persisted in state.
- [ ] UI controls wired to slice settings.
- [ ] Slice generation uses settings (BPM/subdivisions/sample count).
- [ ] Transient detection toggle.
- [ ] Layering + merge mode selection wired to chain builder.

## D. Preview & Playback
- [ ] Preview chain build status updates in UI.
- [ ] Loop playback toggle for chain.
- [ ] One-shot playback for chain.
- [ ] Per-slice playback in grid.

## E. UI/UX & Interaction
- [ ] Waveform rendering (focus view).
- [ ] Waveform rendering (grid thumbnails).
- [ ] Grid mouse/modifier interactions (Shift/Cmd behaviors).
- [ ] Focus view modifier interactions (hover playhead, stutter highlight).
- [ ] Keyboard shortcuts (Shift+A/M/J/R/E, Space, Cmd+C/V/Z).
- [ ] Notification sounds (bleep/cowbell).

## F. Audio Format & DSP
- [ ] Audio conversion pipeline for non‑44.1k/16‑bit/mono sources.
- [ ] Live recording output format is 16‑bit PCM.
- [ ] Live recording storage path matches Application Support.
- [ ] Normalization and/or fades if required by parity.

## G. Export
- [ ] Export dialog UI (prefix + checkboxes).
- [ ] Export preference persistence.
- [ ] Export execution wiring (slice + chain, volume aware).

## H. Live Recording UX
- [ ] Live module metadata (liveModules.json equivalent).
- [ ] Multiple live module slots.
- [ ] Single‑tap clear + double‑tap delete.
- [ ] Persist device/channel per module.

## I. Performance / Logging / Errors
- [ ] Progress updates for long-running tasks (recache/slice/export).
- [ ] Status bar errors for processing failures.
- [ ] Unified logging across audio ops.

## J. Tests
- [ ] Add test coverage or equivalent smoke checks for critical flows.
