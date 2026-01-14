# Parity Checklist — Swift Audit → JUCE Tasks

Use this checklist to track parity completion against the Swift audit. Each item maps to a Swift behavior gap or partial match from the audit summary.

## Lifecycle
- [ ] Splash screen + timed transition before main UI. (T10.1)
- [ ] Startup cleanup of live recording buffers. (T10.2)
- [ ] Window size matches Swift (620×615).

## Source/Cache
- [ ] Directory picker wired to state. (T1.1)
- [ ] Single file picker wired to state. (T1.2)
- [ ] Security-scoped bookmark or platform-equivalent persistence.
- [ ] AudioCache.json read/write support. (T1.3)
- [ ] Cache population after scan (path/duration/sample rate/channel count). (T1.4)
- [ ] Recache flow + progress updates. (T1.5)

## Audio
- [ ] Audio conversion pipeline for non‑44.1k/16‑bit/mono sources. (T2.1, T2.2)
- [ ] Live recording output format is 16‑bit PCM. (T8.1)
- [ ] Live recording storage path matches Application Support. (T8.2)
- [ ] Normalization and/or fades if required by parity.

## UI
- [ ] BPM/subdivision/sample settings persisted in state. (T3.1)
- [ ] UI controls wired to slice settings. (T3.2)
- [ ] Slice generation uses settings (BPM/subdivisions/sample count). (T3.3)
- [ ] Transient detection toggle. (T3.4)
- [ ] Layering + merge mode selection wired to chain builder. (T3.5)
- [ ] Preview chain build status updates in UI. (T12.1)
- [ ] Loop playback toggle for chain. (T4.1)
- [ ] One-shot playback for chain. (T4.1)
- [ ] Per-slice playback in grid. (T4.2)
- [ ] Waveform rendering (focus view). (T5.1)
- [ ] Waveform rendering (grid thumbnails). (T5.2)
- [ ] Grid mouse/modifier interactions (Shift/Cmd behaviors). (T5.3)
- [ ] Focus view modifier interactions (hover playhead, stutter highlight). (T5.4)
- [ ] Keyboard shortcuts (Shift+A/M/J/R/E, Space, Cmd+C/V/Z). (T11.1, T11.2)
- [ ] Notification sounds (bleep/cowbell).

## Export
- [ ] Export dialog UI (prefix + checkboxes). (T7.1)
- [ ] Export preference persistence. (T7.2)
- [ ] Export execution wiring (slice + chain, volume aware). (T7.3)

## Live
- [ ] Live module metadata (liveModules.json equivalent). (T8.3)
- [ ] Multiple live module slots. (T9.1)
- [ ] Single‑tap clear + double‑tap delete. (T9.2)
- [ ] Persist device/channel per module. (T9.3)

## Logging
- [ ] Progress updates for long-running tasks (recache/slice/export). (T1.5, T12.1)
- [ ] Status bar errors for processing failures. (T12.1)
- [ ] Unified logging across audio ops. (T12.2)

## Tests
- [ ] Add test coverage or equivalent smoke checks for critical flows.
