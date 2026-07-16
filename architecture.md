# Current — Architecture (as of Phase 2)

This documents the code as it stands after Phase 2 (canvas skeleton) of
`generative-midi-plugin-requirements.md`. It is written for future sessions:
what exists, why it is shaped this way, and where the seams are for the next
phases. File references are relative to the repo root; headers live in
`include/`, implementations in `source/`.

## What the plugin is right now

A JUCE MIDI-effect plugin (VST3 + Standalone, Linux build only so far). The
editor shows a menu bar (global root / scale / quantize + theme switch), a
canvas that modules can be dragged onto from a palette of four (generators Arp
and Random, modulators Quantize and Shift), and an engine that actually runs
those modules — but as a fixed implicit chain with baked-in defaults, because
Phase 2 has no port wiring and no per-module settings. Double-clicking a node
opens an empty settings placeholder dialog.

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
- `MenuBar` (MenuBar.h/.cpp) — global-settings combos and the Quantize toggle,
  bound to the APVTS via attachments, plus the Theme combo. Edit and Load/Save
  menus are deferred phases.
- `Canvas` (Canvas.h/.cpp) — the drop surface and the bridge between on-screen
  nodes and the processor's module model.
- `ModuleComponent` (ModuleComponent.h/.cpp) — one draggable node. Square for
  generators, circle for modulators; colour encodes the family; ports are
  painted but purely decorative until wiring lands.
- `PaletteBar` + `ModuleTypes` (PaletteBar.h/.cpp, ModuleTypes.h) — the
  draggable chips and the module catalogue that drives them.
- `Theme` + `CurrentLookAndFeel` (Theme.h, CurrentLookAndFeel.h) — the shared
  two-scheme (Light/Dark) palette and the LookAndFeel that applies it to stock
  JUCE widgets.
- `InlineDialog` (InlineDialog.h/.cpp) — the in-editor modal helper (the
  SnorkelAudioStandards rule forbids `juce::AlertWindow` / `DialogWindow`).
  Currently backs the settings placeholder; it already supports text fields
  and multiple buttons so real dialogs can grow on it.
- `tools/engine_smoketest.cpp` — headless engine test, see Testing below.

## The canvas model and who owns it

The single source of truth for "what is on the canvas" is
`CurrentAudioProcessor::moduleList`, a `std::vector<ModuleInstance>` where an
instance is an id, a `ModuleType`, and an x/y position. It lives in the
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
  types are present as four `std::atomic<bool>` flags.
- `processBlock` reads those atomics plus the raw parameter values (root,
  scale, quantize — themselves atomics via APVTS) and hands the `Engine` a
  plain `Engine::Config` by value. No locks anywhere.

Presence flags are enough only because position never affects the sound and
there is exactly one implicit chain. When wiring lands and the audio thread
needs real graph topology, this handoff must grow into an immutable graph
snapshot that is swapped in atomically (build on message thread, publish via
atomic pointer / RCU-style), not per-field atomics. That is the single biggest
known seam in the design.

## The engine: fixed implicit chain

Phase 2 has no user wiring, so `Engine::process` hard-codes the signal flow;
the full commentary (including all fixed defaults) is at the top of
`Engine.h`. Summary:

- Host MIDI acts as the implicit MIDI In.
- Generators fire on a 1/16 grid, gate of half a step, and only while the host
  transport is playing. Arp cycles ascending through currently held host notes
  (and swallows those notes, since they are its input); Random plays in-scale
  notes in MIDI 48..72.
- Modulators apply as pure pitch mapping (`mapPitch`): Quantize snaps to the
  global root/scale (also applied when the global quantize toggle is on), then
  Shift transposes +12.
- The same `mapPitch` is applied to note-ons and note-offs so their pitches
  always match — this is the core no-hanging-notes invariant.
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
pick the new module up from the catalogue without further changes. The `IO`
kind exists in the enum (triangle shapes per the requirements) but is not in
the Phase 2 palette.

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
