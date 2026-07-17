# Current — Architecture (as of Phase 2)

This documents the code as it stands after Phase 2 (canvas skeleton) of
`generative-midi-plugin-requirements.md`. It is written for future sessions:
what exists, why it is shaped this way, and where the seams are for the next
phases. File references are relative to the repo root; headers live in
`include/`, implementations in `source/`.

## What the plugin is right now

A JUCE MIDI-effect plugin (VST3 + Standalone, Linux build only so far). The
editor shows a menu bar (global root / scale + theme switch), a canvas that
modules can be dragged onto from a palette of thirteen (generators Random,
Scale, LFO, Chord, and Drone, modulators Arp, Quantize, Scale, Progression,
Shift, and Delay, I/O modules MIDI In and Output), and an engine that actually runs
those modules — but as a fixed implicit chain, because there is no port
wiring yet. The I/O modules carry a per-module MIDI channel; every other
module carries full settings (drawn from the shared settings pool —
root/scale override, rate, repeat, mode, octaves, gate, plus their
type-specific fields such as the LFO's shape/cycle/depth/phase, Quantize's
swing, the Progression's step list, Shift's amount + chromatic-or-degrees
scale, the Delay's feedback + per-echo shift, the Chord's
degree/type/inversion, and the Drone's voicing/octave), all edited through
real double-click dialogs.

## Component map

- `CurrentAudioProcessor` (PluginProcessor.h/.cpp) — the plugin. Owns the
  APVTS (global parameters), the canvas model, and the `Engine`.
- `Engine` + `ScaleTables` (Engine.h/.cpp, ScaleTables.h) — the audio-thread
  MIDI processing. Knows nothing about JUCE components or the canvas model;
  it receives a plain config snapshot each block.
- `CurrentAudioProcessorEditor` (PluginEditor.h/.cpp) — top-level editor.
  Derives from `juce::DragAndDropContainer` (it must be a common ancestor of
  the palette and the canvas for JUCE drag-and-drop to connect them). Owns the
  `CurrentLookAndFeel`, hosts `MainView`, provides `showInlineDialog()` and
  `applyTheme()`.
- `MainView` (MainView.h/.cpp) — pure layout: `MenuBar` top, `Canvas` middle,
  `PaletteBar` bottom.
- `MenuBar` (MenuBar.h/.cpp) — global-settings combos (root, scale), bound to
  the APVTS via attachments, plus the Theme combo. In the Standalone it also
  carries the internal transport's Play toggle and Tempo stepper (see
  Transport and clocks below); plugin wrappers never show them. Edit and
  Load/Save menus are deferred phases.
- `Canvas` (Canvas.h/.cpp) — the drop surface and the bridge between on-screen
  nodes and the processor's module model.
- `ModuleComponent` (ModuleComponent.h/.cpp) — one draggable node. Square for
  generators, circle for modulators, triangle for I/O; colour encodes the
  family; an optional sublabel shows the I/O channel; ports are painted but
  purely decorative until wiring lands.
- `PaletteBar` + `ModuleTypes` (PaletteBar.h/.cpp, ModuleTypes.h) — the
  draggable chips and the module catalogue that drives them.
- `Theme` + `CurrentLookAndFeel` (Theme.h, CurrentLookAndFeel.h) — the shared
  two-scheme (Light/Dark) palette and the LookAndFeel that applies it to stock
  JUCE widgets.
- `InlineDialog` (InlineDialog.h/.cpp) — the in-editor modal helper (the
  SnorkelAudioStandards rule forbids `juce::AlertWindow` / `DialogWindow`).
  Backs every settings dialog; it supports text fields, combo boxes (including
  same-row pairs and post-show add/remove for dynamic dialogs like the
  Progression's step list), closing action buttons, and non-closing utility
  buttons.
- `tools/engine_smoketest.cpp` — headless engine test, see Testing below.

## The canvas model and who owns it

The single source of truth for "what is on the canvas" is
`CurrentAudioProcessor::moduleList`, a `std::vector<ModuleInstance>` where an
instance is an id, a `ModuleType`, an x/y position, and a channel (the I/O
modules' one setting; other types ignore it). It lives in the
processor, not the editor, so the layout survives the editor being closed and
reopened, and so `get/setStateInformation` can persist it in the DAW project
(a `<Canvas>` child appended to the APVTS tree). This is DAW-project
persistence only — the user-facing Load/Save of the requirements is a later
phase.

The `Canvas` component mirrors that model as `ModuleComponent` children:

- On construction it builds one node per model entry.
- User actions write through to the model (`addModule`, `moveModule`,
  `removeModule`) and update the mirrored components locally.
- If the model is replaced behind the editor's back — a host restoring project
  state while the editor is open — the processor fires
  `canvasModelReplaced` (a `ChangeBroadcaster`) and the canvas rebuilds. Its
  own edits don't go through that path because it already knows what changed.

Module ids are session-local (`nextModuleId` counter, reassigned on state
load); nothing outside the model/canvas pair should hold onto them across a
restore. Persistence stores the module type as a stable string
(`moduleTypeToString`), not the enum value, so saved layouts survive enum
reorders.

## Threading model

The rule in one line: the module list is message-thread only; the audio thread
reads a lock-free snapshot.

- All canvas edits and state load/save happen on the message thread.
- After every model change, `refreshEngineConfig()` republishes which module
  types are present as `std::atomic<bool>` flags, two atomic 16-bit channel
  masks carrying the I/O modules' settings (input filter, output stamp —
  semantics documented on `Engine::Config`), and a set of atomic ints/bools
  carrying each settings-bearing module type's first instance (the implicit
  chain runs one of each; extra copies share the first one's settings). The
  Progression's step list rides in a fixed array of atomics, one packed int
  per step (degree + biased octave) plus a count.
- `processBlock` reads those atomics plus the raw parameter values (root,
  scale — themselves atomics via APVTS) and hands the `Engine` a plain
  `Engine::Config` by value. No locks anywhere. Each field is independently
  atomic; a block seeing a half-updated combination is indistinguishable from
  the edit landing one block later, so no seqlock is needed.

Presence flags and two masks are enough only because position never affects
the sound and there is exactly one implicit chain. When wiring lands and the audio thread
needs real graph topology, this handoff must grow into an immutable graph
snapshot that is swapped in atomically (build on message thread, publish via
atomic pointer / RCU-style), not per-field atomics. That is the single biggest
known seam in the design.

## Transport and clocks

Every grid the engine plays on — the stepped modules' step clocks, the
Arp/Scale repeat windows, the LFO cycle, the Progression playhead, Quantize's
swung boundaries — is re-derived each block from the host's ppq position (the
LAM master-clock model): a boundary fires when it falls inside the block's
half-open `[blockStart, blockEnd)` quarter-note range. There are no
freewheeling counters, so pressing play mid-bar lands the first step on the
song's next real grid point, host loop wraps put the pattern exactly where the
song is, and tempo automation cannot accumulate drift. Floor/ceil/mod are the
mathematical versions, so a negative position (host pre-roll) stays on the
same grid.

Hosts that give the engine nothing to sync to get an internal transport,
synthesized in `processBlock` before the engine runs, so the engine keeps a
single code path:

- The Standalone's playhead reports `isPlaying == false` forever (there is no
  host transport), so the menu bar's Play toggle and Tempo stepper drive a
  processor-owned position instead: ppq accumulates across blocks and rewinds
  to 0 on every Play press, so playback always starts at the top of bar 1.
  Tempo is a runtime preference (not patch content, not an APVTS parameter)
  and resets to 120 each launch.
- A plugin host with no playhead at all free-runs the same way with
  `isPlaying` always true.
- A host playhead that merely lacks a ppq value keeps its own
  `isPlaying`/BPM; the engine falls back to counting quarter notes from
  transport start for that host (`Engine::fallbackQn`).

## The engine: fixed implicit chain

There is no user wiring yet, so `Engine::process` hard-codes the signal flow;
the full commentary (including all fixed defaults and the I/O channel
semantics) is at the top of `Engine.h`. Summary:

- MIDI In modules filter which host events enter the graph by channel (union
  across modules; "All" = everything). With none placed, the implicit input
  accepts all channels. Filtered events are dropped before they reach anything
  — including the Arp's held-note tracking.
- The stepped modules (Arp, Random, Scale, LFO, Chord, Drone) fire only while
  the transport is playing, each on its own step grid anchored to the song's
  bar 0 (see Transport and clocks). Arp walks the currently
  held host notes per its mode (Up / Down / Up-Down / Random) across its
  octave span at its rate and gate, swallowing those notes since they are its
  input; Random draws uniformly from its scale within its note range at its
  rate; Scale walks its scale from the root at octave 3 across its octave
  span, up or down; the LFO evaluates its shape at the current position
  inside its bar-length cycle (plus the start-phase offset) and maps the
  bipolar value to scale degrees around the root at octave 3, swinging ± its
  depth (octaves + scale steps); the Chord emits its diatonic stack (degree +
  type + inversion on its root/scale) on a bar-based period/length grid; the
  Drone holds its voicing on the same period/length model, re-triggering
  immediately when the mapped pitch set changes mid-hold (drone-flagged
  entries in `activeGen`) — it bypasses Quantize and the Delay by design (see
  `Engine.h`). Arp and Scale reset their pattern every
  repeat interval (windows counted on the grid from the song's bar 0 — longer
  patterns truncate, shorter ones rest); a repeat of Endless publishes as 0 qn,
  meaning no window (the Arp walk never resets, the Scale pattern loops
  back-to-back). Root/scale overrides of -1 fall back to the globals. The
  shared option tables and the per-module settings blob live in
  `ModuleSettings.h` (GUI-free), used by the engine config, the processor,
  and the canvas dialogs alike.
- Pitch modulators apply as a mapping chain (`mapPitch`): the Scale modulator
  snaps to its (root, scale); the Progression transposes to its current step
  (degree via `ScaleTables::stepInScale`, octave chromatic — the step is
  looked up per note-on from the quarter-note position the note will sound
  at, and note-offs stay safe because they release from the activeGen /
  activePass bookkeeping, not from re-mapping); then Shift transposes by its
  amount — scale degrees via `stepInScale` when its scale is Global/named,
  chromatic semitones when Off (`ModuleOptions::kScaleOff`, stored in the
  shared `scaleOverride` field).
- Quantize is the second stateful time modulator: while playing, every
  note-on leaving the chain is deferred to the next point of its rate grid
  (`pendingQuant`), with swing pushing odd grid points late by swing/2 of a
  step (pair-based model, `swing-timing.md`). Generated notes keep their
  gate; host-held notes register in `activePass` when they finally sound, and
  a host note-off that beats its own deferred note-on converts the entry to a
  fixed duration instead. The queue is discarded on transport stop; when
  stopped, Quantize passes everything through (no grid). The grid (and the
  swing parity) is derived from the song position, so a module dropped
  mid-play joins the song's grid — and the shuffle sits on the song's bars,
  not on wherever play was pressed.
- The Delay is the exception to pure mapping — it is the first stateful time
  modulator. Every emitted note-on (pass-through and generated) books an echo
  in `pendingEchoes` (velocity × feedback, pitch + per-echo shift, one delay
  time later); fired echoes book their successors until the velocity decays
  below a floor or the pitch leaves the MIDI range. Echoes derive from the
  final emitted stream (post Quantize/Shift/Output), live in `activeGen` for
  their gate-timed release, run regardless of transport, and the pending
  queue is discarded on transport stop (the requirements' shared transport
  rule for stateful time modules).
- Output modules stamp everything leaving the engine with their channel; with
  several, the stream is duplicated once per channel (the implicit chain's
  fan-out). With none placed, events keep their own channel.
- The same `mapPitch` is applied to note-ons and note-offs so their pitches
  always match — this is the core no-hanging-notes invariant. Passed-through
  host notes are additionally remembered in `activePass` as
  (incoming → emitted) pairs, so a note-off releases exactly what its note-on
  emitted even if a setting (e.g. an Output channel) changed mid-note.
- Generated notes are tracked in `activeGen` with a remaining-samples count;
  on transport stop (or all modules removed) everything still sounding gets a
  note-off, per the requirements' transport-boundary rule.
- With no modules on the canvas, MIDI passes through untouched.

The engine deliberately has no JUCE GUI dependencies and takes everything it
needs as arguments, which is what makes the headless smoke test possible.

## Module catalogue: how to add a module

`ModuleTypes.h` is the one place that defines what modules exist. To add one:

- Extend the `ModuleType` enum and `moduleCatalogue()` (name + kind).
- Add the string mapping in `moduleTypeToString` / `moduleTypeFromString`
  (stable id for persistence).
- Give it behaviour in the engine (today that means a flag in
  `Engine::Config`, a branch in `refreshEngineConfig()`, and logic in
  `Engine::process`; post-wiring it will mean a node implementation).

Palette, drag-and-drop, canvas nodes, selection, deletion, and persistence all
pick the new module up from the catalogue without further changes. All three
kinds are in the palette: generators square, modulators circle, I/O triangles
(MIDI In points right, Output left, toward their single port). Per-module
settings live on `ModuleInstance`: the I/O modules' MIDI channel and the
shared `ModuleSettings` blob (used by every other type), each edited via a
real settings dialog in `Canvas` (`openChannelDialog`, `openArpDialog`,
`openRandomDialog`, `openScaleGenDialog`, `openLfoDialog`,
`openQuantizeDialog`, `openScaleModDialog`, `openProgressionDialog`,
`openShiftDialog`, `openDelayDialog`, `openChordDialog`, `openDroneDialog`),
reflected in a node sublabel (channel, rate, Shift's signed amount, the
Scale modulator's scale, the Progression's degrees, the Chord's degree +
type, or the Drone's voicing), and persisted with the canvas state. The dialogs
build their combos through `Canvas`'s shared add/read helper pairs
(root+scale, rate, repeat, mode, octaves, hold length+repeat) so a shared setting is the
identical control in every module — modules.md's "Shared settings" section is
the product-level rule. The root/scale lists are sourced from the APVTS
choice parameters ("Global" prepended), so they can't drift from the menu
bar; Shift's dialog inserts its extra "Off" entry into that same list. The
Progression dialog is the first dynamic one: its step rows (degree + octave
side by side via `InlineDialog`'s same-row combos) are added and removed live
by non-closing utility buttons, and the panel re-lays itself out.

## Theming

`CurrentTheme` (Theme.h) holds two `Scheme` structs (Light/Dark) following the
SnorkelAudioStandards two-theme convention. Everything paints by reading
`CurrentTheme::active()` fresh every frame, so a theme swap is: set the active
scheme, re-apply the LookAndFeel colours (`applyScheme`), repaint the editor
root. The Theme choice is an APVTS parameter so it persists with the project;
`applyTheme()` in the editor syncs the active scheme from the parameter before
first paint (avoids a wrong-theme flash) and after combo changes. Family
colours for nodes (generator green, modulator purple, I/O blue) live in the
scheme so both themes can tune them.

## UI interaction details worth knowing

- Drag-and-drop from palette to canvas uses JUCE's `DragAndDropContainer`
  machinery; the drag description is a tagged string
  (`"module:<TypeName>"`, built by `Canvas::makeDragDescription`) so the
  canvas can recognise palette drags cheaply.
- Node dragging uses `ComponentDragger` with a bounds constrainer that keeps
  nodes fully inside the canvas.
- Selection is single-select; Delete/Backspace removes the selected node (a
  Phase 2 convenience — the full delete/duplicate context menu is a later
  phase). Clicking empty canvas deselects and grabs keyboard focus so the
  Delete key works.
- Marquee select, pan, zoom, and connect gestures are all later phases.

## Build and test

- `./build_and_run_linux.sh` builds VST3 + Standalone (`build-linux/`),
  installs the VST3 to `~/.vst3`, launches the Standalone; `--no-run` for
  headless builds. JUCE 8.0.12 arrives via CMake FetchContent on first
  configure.
- The processor exposes a stereo audio bus it never fills because some VST3
  hosts (notably Live) reject an effect plugin with no audio bus; processBlock
  clears the buffer and only touches MIDI.
- `-DCURRENT_BUILD_TESTS=ON` adds `current_engine_test`, a headless console
  app (tools/engine_smoketest.cpp) asserting the engine's fixed-default
  behaviours and, above all, note-on/note-off balance across pass-through,
  modulators, generators, and transport stop. Run it after any engine change.
- The VST3 passes pluginval at strictness 10 (including Steinberg's embedded
  vst3validator — which is what required the named default program in
  getProgramName, and the editor/state/automation stress tests). pluginval
  isn't vendored; build it from github.com/Tracktion/pluginval and run
  `pluginval --strictness-level 10 --validate ~/.vst3/Current.vst3` (under
  Xvfb if headless). Worth re-running before releases and after processor /
  parameter / state changes.
- macOS / Windows / iPad targets and their build scripts are not ported yet;
  CMakeLists.txt already carries the universal-binary setup for macOS.

## Deferred, by design

Port wiring and the real graph (with the snapshot handoff described above),
per-module settings (the InlineDialog placeholder is the hook), the full
module set from the requirements, I/O modules on the canvas, marquee
multi-select / copy / paste / duplicate, pan and zoom, undo/redo, user preset
Load/Save (follow `preset-system-guideline.md` / LAM when it lands), touch
gestures, and the non-Linux targets.
