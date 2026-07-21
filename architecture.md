# Current — Architecture

This documents the code as it stands now that port wiring — the product's
core premise — is implemented on top of the Phase 2 canvas skeleton of
`generative-midi-plugin-requirements.md`. It is written for future sessions:
what exists, why it is shaped this way, and where the seams are for the next
phases. File references are relative to the repo root; headers live in
`include/`, implementations in `source/`.

## What the plugin is right now

A JUCE MIDI-effect plugin (VST3 + Standalone + macOS AU). The editor shows a
menu bar (global root / scale + theme switch), a canvas that modules can be
dragged onto from a palette of seventeen (generators Random, Scale, LFO,
Chord, and Drone, modulators Arp, Quantize, Scale, Progression, Shift,
Mirror, Harmonizer, Delay, Strum, and Humanize, I/O modules MIDI In and
Output), **port wiring** — cables dragged from output ports to input ports
that define the signal flow — and an engine that executes exactly the wired
graph: host MIDI enters only through MIDI In nodes, only what reaches an
Output node leaves, an unwired module is silent, fan-out duplicates a stream
and fan-in merges. A fresh instance starts with MIDI In → Output wired, so it
passes MIDI through out of the box. The I/O modules carry a per-module MIDI
channel; every other module carries full settings (drawn from the shared
settings pool — root/scale override, rate, repeat, mode, octaves, gate, plus
their type-specific fields such as the LFO's shape/cycle/depth/phase,
Quantize's swing, the Progression's step list, Shift's amount +
chromatic-or-degrees scale, the Delay's feedback + per-echo shift, the
Chord's degree/type/inversion, and the Drone's voicing/octave), all edited
through real double-click dialogs. Every placed module is independent — its
own settings, its own runtime state.

## Component map

- `CurrentAudioProcessor` (PluginProcessor.h/.cpp) — the plugin. Owns the
  APVTS (global parameters), the canvas model (modules + connections), and
  the `Engine`.
- `EngineGraph.h` — the graph snapshot handed to the audio thread: per-node
  `ModuleParams` (settings resolved to engine units), `GraphNode` (id, type,
  params, upstream indices), `GraphSnapshot` (topologically sorted nodes +
  topologyVersion). GUI-free, shared by processor, engine, and the smoke test.
- `Engine` + `ScaleTables` (Engine.h/.cpp, ScaleTables.h) — the audio-thread
  MIDI processing: executes the GraphSnapshot node by node. Knows nothing
  about JUCE components or the canvas model.
- `CurrentAudioProcessorEditor` (PluginEditor.h/.cpp) — top-level editor.
  Derives from `juce::DragAndDropContainer` (it must be a common ancestor of
  the palette and the canvas for JUCE drag-and-drop to connect them). Owns the
  `CurrentLookAndFeel`, hosts `MainView`, provides `showInlineDialog()` and
  `applyTheme()`.
- `MainView` (MainView.h/.cpp) — pure layout: `MenuBar` top, `Canvas` middle,
  `PaletteBar` bottom. Also owns the `SettingsView` and the swap: the menu
  bar's Settings button toggles everything below the bar (canvas + palette —
  a palette over a settings page would invite meaningless drags) between the
  two surfaces by visibility; the settings space always has the full
  below-the-bar bounds.
- `MenuBar` (MenuBar.h/.cpp) — global-settings combos (root, scale), bound to
  the APVTS via attachments, plus the Settings button at the far right (reads
  "Back" while the settings space is open; the theme switch that used to sit
  there lives in the settings space now). In the Standalone it also
  carries the internal transport's Play toggle and Tempo stepper (see
  Transport and clocks below); plugin wrappers never show them. Edit and
  Load/Save menus are deferred phases.
- `SettingsView` (SettingsView.h/.cpp) — the settings space, laid out in
  LAM's Settings-tab language (stacked rounded panels, a section-name column
  followed by uniform label-over-control columns): a Global panel holding the
  Theme button (click cycles the choice param; an APVTS listener keeps label
  and skin in sync with external writes), and an Audition Synth panel — enable
  toggle plus the five tone dials in signal-flow order, each label a live
  "name: value" readout (the `ModuleWindow` dial idiom, standing in for LAM's
  messaging area). The synth panel hides where the synth can't be heard
  (`isAuditionSynthSupported()`, i.e. the AU MIDI-FX).
- `AuditionSynth` (AuditionSynth.h/.cpp) — the built-in monitoring synth,
  ported verbatim from LAM (supersaw pluck voices, TPT low-pass with its own
  fast envelope, sub-octave sine; delay → reverb insert chain on one Space
  dial; CC 70/71/74/75 modulation; raised-cosine declick on the enable
  toggle). `processBlock` feeds it the outgoing MIDI right after
  `engine.process`, so it voices exactly what the plugin emits. Voiceless
  (and skipped entirely) in the AU, the one wrapper with no audio output bus.
  Two deliberate divergences from LAM: the amp envelope is pinned to the
  sustaining shape (Current's note lengths are user-authored content — Gate,
  Length, the Drone's holds — so the voice must honour note-offs), and the
  enable state defaults ON in the Standalone / OFF in DAWs via a runtime seed.
- `Canvas` (Canvas.h/.cpp) — the drop surface and the bridge between on-screen
  nodes and the processor's model. Owns the connect gesture (live cable from
  an output port to the drop target), paints the cables (bezier curves with a
  flow arrow, straight from the model — no per-cable components), and handles
  cable selection/deletion.
- `ModuleComponent` (ModuleComponent.h/.cpp) — one draggable node. Square for
  generators, circle for modulators, triangle for I/O; colour encodes the
  family; an optional sublabel shows the I/O channel. Ports are live: a press
  on the output port (enlarged hit radius) starts the connect gesture, which
  the canvas runs via the onPortDrag* callbacks. While selected it shows an ✕
  badge (top-right, family-coloured) — the touch path to deletion, reported
  via onDelete; the node-move drag stream (onNodeDrag/onNodeDragEnd) feeds the
  tray remove zone.
- `PaletteBar` + `ModuleTypes` (PaletteBar.h/.cpp, ModuleTypes.h) — the
  draggable chips and the module catalogue that drives them (catalogue order =
  display order: I/O first, then generators, then modulators; chips show the
  descriptor's `shortName`). Chips keep full size at every window width — they
  live in a horizontal `Viewport` strip with the scrollbar on the tray's
  bottom edge — and a checkbox row above (In/Out / Generators / Modulators,
  per-editor state, not persisted) hides whole families. The tray doubles as
  the remove zone for the drag-a-node-here delete gesture (armed/hot states
  painted in `paintOverChildren`; `MainView` wires it to the canvas).
- `Theme` + `CurrentLookAndFeel` (Theme.h, CurrentLookAndFeel.h) — the shared
  two-scheme (Light/Dark) palette and the LookAndFeel that applies it to stock
  JUCE widgets.
- `InlineDialog` (InlineDialog.h/.cpp) — the in-editor modal helper (the
  SnorkelAudioStandards rule forbids `juce::AlertWindow` / `DialogWindow`).
  Now backs only the **generic fallback** for a module type that has no
  dedicated `ModuleWindow` yet (every real module has one), so it is trimmed to
  what that needs: a title, an optional wrapped message, and action buttons. The
  text-field and dynamic combo-row machinery it once carried for the module
  dialogs was removed when the last module, Progression, moved to `ModuleWindow`.
- `ModuleWindow` (ModuleWindow.h/.cpp) — the redesigned per-module settings
  surface every module shares (same modal-overlay and disposal contract as
  `InlineDialog`, so both obey the modal-dialog rule). Fixed structure: a title,
  a thin menu bar echoing the global one (three slots — Root, Scale, and a Rate
  or Length combo, unused slots left blank), a 3x2 grid of combo/dial cells
  (labels above, Little Arp Monster section-box sizing; dials for knob-friendly
  values like octaves/gate), and an action-button row. All seventeen modules are
  on it, routing shared settings through `Canvas`'s `ModuleWindow` helper pairs.
  For a module whose body the six cells can't hold, `setCustomBody` swaps the
  grid for a caller-supplied component and sizes the panel to it, keeping the
  rest of the chrome — **Progression** uses this for its step list (see below).
  Three hooks let one cell react to another: `setComboChangeCallback` and
  `refreshDial` (wrapped in `Canvas`'s shared `addAmountDial`/`readAmountDial`
  pair so Shift/Delay's amount dial rewords its unit ("steps"/"semitones") when
  the Scale combo flips), and `setDialChangeCallback`/`setDialValue` (Mirror's
  Low and High dials push each other so the window can't invert). The design decisions behind
  the window (band-by-band rules, dial readouts, the roads not taken) are
  written up in `design/module-window.md`.
- `tools/engine_smoketest.cpp` — headless engine test, see Testing below.
- `tools/audition_smoketest.cpp` — headless end-to-end audition-path test,
  see Testing below.

## The canvas model and who owns it

The single source of truth for "what is on the canvas" is the pair
`CurrentAudioProcessor::moduleList` + `connectionList`: a
`std::vector<ModuleInstance>` (id, `ModuleType`, x/y position, channel,
settings blob) and a `std::vector<ModuleConnection>` (fromId → toId; modules
have at most one port per direction, so the two ids identify a cable
completely). Both live in the processor, not the editor, so the patch
survives the editor being closed and reopened, and so
`get/setStateInformation` can persist it in the DAW project (a `<Canvas>`
child appended to the APVTS tree, with `<Module>` and `<Connection>`
children). This is DAW-project persistence only — the user-facing Load/Save
of the requirements is a later phase.

Connection validity lives in one place, `canConnect`: both modules exist, the
ports exist (generators/MIDI In have no input, Output has no output — see
`moduleHasInputPort`/`moduleHasOutputPort` in ModuleTypes.h), no self-loop,
no duplicate, and no cycle (DFS at connect time), so the engine can always
run the graph in topological order. `removeModule` drops the module's cables
with it. A fresh processor constructs the default patch (MIDI In → Output,
wired) — except in the Standalone, which boots an **empty canvas**: the
Standalone is the dev/test app, so it deliberately reports no state at all
(`get/setStateInformation` early-return there, the LAM approach) and every
launch is a clean slate. Its two keep-worthy preferences — theme and manual
tempo — survive in a small `PropertiesFile`
(`~/Library/Application Support/Snorkel Audio/Current.settings`) instead, and
the audition synth re-seeds ON each launch.

The `Canvas` component mirrors that model as `ModuleComponent` children:

- On construction it builds one node per model entry; cables are painted
  directly from `connections()`, so they need no mirroring.
- User actions write through to the model (`addModule`, `moveModule`,
  `removeModule`, `addConnection`, `removeConnection`) and update the
  mirrored components locally.
- If the model is replaced behind the editor's back — a host restoring project
  state while the editor is open — the processor fires
  `canvasModelReplaced` (a `ChangeBroadcaster`) and the canvas rebuilds. Its
  own edits don't go through that path because it already knows what changed.

Module ids are session-local (`nextModuleId` counter, reassigned on state
load); nothing outside the model/canvas pair should hold onto them across a
restore. Persistence stores the module type as a stable string
(`moduleTypeToString`), not the enum value, so saved layouts survive enum
reorders — and cables by the modules' *positions in the saved list*, not
their ids, for the same reason.

## Threading model

The rule in one line: the model is message-thread only; the audio thread
reads an immutable graph snapshot.

- All canvas edits and state load/save happen on the message thread.
- After every model change, `rebuildGraph()` bakes the model into a fresh
  `GraphSnapshot` (per-node settings resolved from option-table indices to
  engine units, nodes topologically sorted, upstream links as indices) and
  publishes the `shared_ptr` under a `juce::SpinLock`.
- `processBlock` *try*-locks to adopt the newest snapshot — on a lost race it
  simply keeps last block's graph one block longer — and hands the engine the
  raw pointer plus the global root/scale (APVTS atomics). The audio thread
  never blocks; releasing a superseded snapshot may deallocate on the audio
  thread, which this codebase already accepts (the engine's own containers
  grow there too).
- Sounding-note policy across edits rides on `topologyVersion`: modules or
  cables added/removed bump it, and the engine responds by releasing
  everything sounding at the host and clearing all per-node state (coarse but
  safe — in-flight bookkeeping can't be trusted across an arbitrary rewire).
  Settings-only edits republish under the same version; per-node state is
  keyed by module id, so notes keep ringing through them.

This is the immutable-snapshot handoff the fixed-chain era predicted; the
per-module-type atomics it replaced are gone.

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

## The engine: graph execution

`Engine::process` executes the published `GraphSnapshot` each block; the full
commentary (per-node behaviours, the no-hanging-notes contract, transport
rules) is at the top of `Engine.h`. The execution model:

- The host's incoming events are set aside; nodes run front to back (the
  snapshot arrives topologically sorted). Each node merges its wired inputs'
  output buffers (fan-in), transforms, and writes its own output buffer;
  fan-out is just two consumers reading the same upstream buffer. MIDI In
  nodes read the host events (channel-filtered); the Output nodes' buffers
  are concatenated back into the host buffer. Strict semantics: no implicit
  routing, no Output = silence.
- Per-node state (held input notes, gate countdowns, pending time-shifted
  buffers, in-flight note maps) lives in `Engine::states`, keyed by module
  id, so it survives settings edits. A topologyVersion change (rewire) emits
  note-offs for everything in `hostSounding` — the engine's record of what is
  currently sounding *at the host* — and clears all node state.
- The no-hanging-notes invariant is per node: whatever emits a note-on
  remembers it and emits the matching note-off itself — from the matching
  input note-off (mapped through the remembered pitch/channel, so mid-note
  settings edits can't hang anything) or from its own gate. A note-off with
  no remembered note-on is swallowed, so dropped/discarded notes can't leak
  stray releases downstream.

The per-module behaviours carried over from the fixed-chain engine unchanged
(step grids anchored to the song's bar 0, pair-based swing, deterministic
jitter, the shared stop rules), now scoped per node. The notable semantic
shifts that came with wiring:

- The Arp and Harmonizer act on *their wired input* — any stream, generated
  included, not just played host notes as in the fixed chain. The Arp
  consumes its input notes while playing (they are its data) and passes them
  through while stopped; the Harmonizer stacks voices on every input note-on
  per its Mode (Add/Replace/Top over its input held set).
- The pitch mappers (Scale, Progression, Shift, Mirror) are separate nodes
  sharing one skeleton (`mapNoteStream`): map the note-on's pitch, remember
  (in → out), release the remembered pitch on the note-off. Mirror's Limit
  mode returns -1 to drop a note (nothing emitted or booked). Wire them in
  any order — the old fixed sequence is just one possible patch.
- Quantize/Delay/Strum/Humanize keep their block-relative pending buffers per
  node, so several instances can live on different branches. The Drone no
  longer secretly bypasses Quantize/Delay — routing decides (and a Drone
  through a Progression no longer re-triggers on a step change mid-hold; it
  picks the new degree up at its next window).
- Generators emit on channel 1; Output nodes stamp their channel (remembering
  per note what they stamped, so a mid-note channel edit can't hang), and
  MIDI In admits a note-off iff it admitted the note-on.
- The stop contract moved into the nodes: generators/Arp flush their gate
  lists, Quantize/Delay/Strum/Humanize discard buffered material, and the
  Strum/Humanize stop paths track offs owed to dropped note-ons
  (`swallowOffs`) so no stray release leaks downstream.

The shared option tables and the per-module settings blob stay in
`ModuleSettings.h` (GUI-free), used by the graph builder, the processor, and
the canvas dialogs alike; root/scale overrides of -1 still fall back to the
globals inside each node.

The engine deliberately has no JUCE GUI dependencies and takes everything it
needs as arguments, which is what makes the headless smoke test possible.

## Module catalogue: how to add a module

`ModuleTypes.h` is the one place that defines what modules exist. To add one:

- Extend the `ModuleType` enum and `moduleCatalogue()` (name + kind).
- Add the string mapping in `moduleTypeToString` / `moduleTypeFromString`
  (stable id for persistence).
- Decide its ports: `moduleHasInputPort` / `moduleHasOutputPort` in
  ModuleTypes.h derive them from the type (generators and MIDI In are
  sources, Output is the one sink, modulators have both).
- Give it behaviour in the engine: any new settings go into `ModuleParams`
  (EngineGraph.h), a branch in `CurrentAudioProcessor::paramsFor` resolves
  them to engine units, and a `processYourModule` function in Engine.cpp
  (dispatched from `Engine::processNode`) implements the node — input stream
  in, output stream out, per-node state in `Engine::NodeState`.

Palette, drag-and-drop, canvas nodes, wiring, selection, deletion, and
persistence all pick the new module up from the catalogue without further
changes. All three
kinds are in the palette: generators square, modulators circle, I/O triangles
(MIDI In points right, Output left, toward their single port). Per-module
settings live on `ModuleInstance`: the I/O modules' MIDI channel and the
shared `ModuleSettings` blob (used by every other type), each edited via a
real settings dialog in `Canvas` (`openChannelDialog`, `openArpDialog`,
`openRandomDialog`, `openScaleGenDialog`, `openLfoDialog`,
`openQuantizeDialog`, `openScaleModDialog`, `openProgressionDialog`,
`openShiftDialog`, `openMirrorDialog`, `openHarmonizerDialog`,
`openDelayDialog`, `openStrumDialog`,
`openHumanizeDialog`, `openChordDialog`, `openDroneDialog`),
reflected in a node sublabel (channel, rate, Shift's signed amount, the
Scale modulator's scale, the Progression's degrees, the Chord's degree +
type, the Drone's voicing, the Strum's spread in ms, or the Harmonizer's chord
type), and persisted with the
canvas state. The dialogs
build their controls through `Canvas`'s shared add/read helper pairs
(root+scale, rate, repeat, mode, octaves, gate, hold length+repeat) so a shared setting is the
identical control in every module — modules.md's "Shared settings" section is
the product-level rule. The root/scale lists are sourced from the APVTS
choice parameters ("Global" prepended), so they can't drift from the menu
bar; Shift's dialog inserts its extra "Off" entry into that same list.

The settings UI is fully on the structured `ModuleWindow` (see the component
map) for all seventeen modules. Each `open*Dialog` builds a `ModuleWindow` via
`editor.showModuleWindow`, fills the menu bar (Root / Scale / Rate-or-Length)
and grid cells through `Canvas`'s `ModuleWindow` helper pairs, and reads the
controls back by name (`getComboSelectedIndex` / `getDialValue`) on OK. Modules
with a value that reads well as a knob (octaves, gate, and Shift/Delay's amount)
use grid **dials**, rendered through `CurrentLookAndFeel::drawRotarySlider`, with
the value folded into the cell label; the amount dial's unit label is refreshed
off the Scale combo via `setComboChangeCallback`/`refreshDial`.

`openProgressionDialog` is the one dynamic case: instead of grid cells it calls
`ModuleWindow::setCustomBody` with a `ProgressionStepList` — an arranger-style
step row (cells with add/remove append arrows, plus Degree/Octave combos for the
selected step; see `design/module-window.md`). The list holds a working copy of
the steps and hands them back with `getSteps()` on OK. The menu bar still carries
Root / Scale / Length through the usual helpers.

## Theming

`CurrentTheme` (Theme.h) holds two `Scheme` structs (Light/Dark) following the
SnorkelAudioStandards two-theme convention. Everything paints by reading
`CurrentTheme::active()` fresh every frame, so a theme swap is: set the active
scheme, re-apply the LookAndFeel colours (`applyScheme`), repaint the editor
root. The theme control is the settings space's Theme button (`SettingsView`).
The Theme choice is an APVTS parameter so it persists with the project (in the
Standalone, which persists no plugin state, it rides the preferences file
instead — see the canvas-model section); `applyTheme()` in the editor syncs
the active scheme from the parameter before first paint (avoids a wrong-theme
flash) and on every switch. Family colours for nodes (generator green,
modulator purple, I/O blue) live in the scheme so both themes can tune them.

## UI interaction details worth knowing

- Drag-and-drop from palette to canvas uses JUCE's `DragAndDropContainer`
  machinery; the drag description is a tagged string
  (`"module:<TypeName>"`, built by `Canvas::makeDragDescription`) so the
  canvas can recognise palette drags cheaply.
- Node dragging uses `ComponentDragger` with a bounds constrainer that keeps
  nodes fully inside the canvas.
- The connect gesture: a press within `ModuleComponent::kPortHitRadius` of a
  node's output port starts a cable drag (the node forwards the mouse events
  to the canvas); releasing anywhere over a module with an input port — the
  whole node is the target, one input port makes that unambiguous — asks the
  processor to connect. `canConnect` refusals (duplicate, cycle, port
  mismatch) just snap the cable back.
- Selection is single-select across nodes *and* cables (mutually exclusive):
  clicking near a cable selects it (12 px tolerance — fingertip-sized, not
  pointer-sized). Clicking empty canvas deselects and grabs keyboard focus so
  the Delete key works.
- Deletion has one shared path (`Canvas::deleteNode`) and three gestures: the
  ✕ badge a selected node/cable shows (the cable's replaces its flow arrow at
  the midpoint), dragging a node onto the palette tray (the tray arms while a
  node drag is in flight and goes hot under the pointer; release deletes), and
  Delete/Backspace on the selection. A node takes its cables with it. The ✕
  and tray gestures arrive from inside the doomed node's own mouse callback,
  so they defer the removal via `Canvas::requestDeleteNode` (callAsync +
  SafePointer).
- Marquee select, pan, and zoom are later phases.

## Build and test

- `./build_and_run_linux.sh` builds VST3 + Standalone (`build-linux/`),
  installs the VST3 to `~/.vst3`, launches the Standalone; `--no-run` for
  headless builds. JUCE 8.0.12 arrives via CMake FetchContent on first
  configure.
- The processor exposes a stereo audio bus because some VST3 hosts (notably
  Live) reject an effect plugin with no audio bus; processBlock clears it
  every block, and the only thing that ever writes into it is the audition
  synth (after the engine has produced the block's MIDI).
- The Standalone answers `isMidiEffect() == false` (the one wrapper that
  does): JUCE's `AudioProcessorPlayer` hands a MIDI-effect processor a
  zero-channel buffer and zeroes the device output itself, which would mute
  the audition synth. LAM sidesteps the same trap by building its Standalone
  from an `IS_SYNTH` target; Current's single target answers per-wrapper.
- `-DCURRENT_BUILD_TESTS=ON` adds `current_engine_test`, a headless console
  app (tools/engine_smoketest.cpp) asserting the module behaviours and, above
  all, note-on/note-off balance across pass-through, modulators, generators,
  and transport stop. Most tests describe their setup as a `TestConfig` that
  a helper lays out as an explicit graph in the classic chain order; a
  dedicated section exercises fan-out/fan-in, empty-graph silence, and the
  topology-change flush directly. Run it after any engine change.
- The same flag adds `current_audition_test` (tools/audition_smoketest.cpp):
  the full processor headless — Random wired into Output, transport
  free-running, synth enabled — asserting that note-ons leave processBlock
  AND that audio lands in the buffer. It links the whole plugin source list
  (createEditor drags the editor in), which works because Current's bus setup
  is runtime-only, no JucePlugin_* macros. Run it after audition-synth or
  processBlock changes.
- The VST3 passes pluginval at strictness 10 (including Steinberg's embedded
  vst3validator — which is what required the named default program in
  getProgramName, and the editor/state/automation stress tests). pluginval
  isn't vendored; build it from github.com/Tracktion/pluginval and run
  `pluginval --strictness-level 10 --validate ~/.vst3/Current.vst3` (under
  Xvfb if headless). Worth re-running before releases and after processor /
  parameter / state changes.
- macOS builds via `./build_and_run_mac.sh` (Universal VST3 + Standalone +
  AU); Windows has an unverified script; the iPad target is not ported.

## Deferred, by design

The full module set from the requirements, marquee multi-select / copy /
paste / duplicate, pan and zoom, undo/redo, user preset Load/Save (follow
`preset-system-guideline.md` / LAM when it lands), touch gestures (including
the tap-output-then-tap-input connect alternative), a mid-hold Progression
follow for held notes (the old fixed chain re-triggered a Drone on a step
change; with wiring the Drone picks the new degree up at its next window),
and the Windows / iPad targets.
