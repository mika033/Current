# Current — Architecture

This document is the head-start reference for anyone continuing work on Current.
It describes the codebase as it stands at the end of **Phase 2 (Canvas
skeleton)** and points at where each future concern will slot in. Read it
alongside `generative-midi-plugin-requirements.md` (the product spec) and
`CLAUDE.md` (working conventions).

## Where we are

Phases are defined in the requirements doc. The code in this repo implements
**Phase 2: Canvas skeleton** in full, and by building the whole editor shell it
also subsumes everything in **Phase 1: Basics** (stub app, menu bar, empty
canvas/palette, Light/Dark theme). So both phases are effectively complete here;
there is no separate "basics-only" build.

Delivered this phase:

- A MIDI-effect plugin that builds and runs on Linux as **VST3 + Standalone**.
- A functional canvas: modules drag in from the palette, move around, and are
  deleted with the Delete/Backspace key.
- A palette of exactly four modules — two generators (**Arp**, **Random**) and
  two modulators (**Quantize**, **Shift**).
- Global **root / scale / quantize** settings, as real automatable parameters.
- The four modules run on **fixed, non-editable defaults** through a small MIDI
  engine, so a placed patch actually makes sound.
- Double-clicking a module opens an **empty settings placeholder** dialog.
- DAW-project state persistence (module layout + globals). This is *not* the
  user-facing Load/Save of presets, which Phase 2 deliberately omits.

Everything is fresh code for Current except two helpers adapted from Little Arp
Monster (LAM): the `Theme` palette and the `InlineDialog`.

## Build

- **Linux:** `./build_and_run_linux.sh` builds VST3 + Standalone into
  `build-linux/`, installs the VST3 into `~/.vst3`, and launches the Standalone.
  Pass `--no-run` to build only (headless/CI — no display to open a window on).
  The first configure fetches JUCE 8.0.12 via `FetchContent` (a few minutes);
  later builds are incremental.
- **Headless engine test:** configure with `-DCURRENT_BUILD_TESTS=ON` to build
  `current_engine_test` from `tools/engine_smoketest.cpp`. It links only the
  `Engine` (no plugin wrappers) and asserts the fixed-default behaviours plus the
  one safety property that matters for a MIDI effect: every note-on is balanced
  by a note-off, so nothing hangs.
- **Linux dev deps (Ubuntu):** `libasound2-dev`, the X11 set (`libx11-dev
  libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev
  libxrender-dev`), `libgl1-mesa-dev`, `libfreetype-dev libfontconfig1-dev`.
- **macOS / Windows / iPad:** not yet ported. Only the Linux script exists so
  far; the mac/win scripts and the AU / AUv3 formats come in a later phase, per
  the shared `build-scripts.md` naming/roles convention.

## The two-layer split

The whole design turns on one boundary: **the message thread owns the model and
the UI; the audio thread owns the engine.** They meet through a lock-free
snapshot, never a shared mutable object.

- **Model + UI (message thread):** `CurrentAudioProcessor`'s module list, the
  editor, the canvas, the palette. All edits (add / move / remove a module)
  happen here.
- **Engine (audio thread):** `Engine`, driven only from `processBlock`. It reads
  a handful of `std::atomic` flags that the message thread republishes whenever
  the model changes. It never touches the module vector directly.

Keep this boundary intact as the plugin grows. The moment `processBlock` needs
to read the actual graph (once connections exist), this snapshot handoff has to
grow into a proper message→audio publish of the whole graph (e.g. a
double-buffered / swapped immutable graph), not a lock around the live model.

## Components

Files live under `include/` (headers) and `source/` (implementations); the
engine test is in `tools/`.

### Processor and model

- **`CurrentAudioProcessor`** (`PluginProcessor.*`) — the MIDI effect. It
  exposes a stereo audio bus it never fills (some VST3 hosts, e.g. Live, reject
  an effect with no audio bus) and clears whatever audio the host hands it.
  `processBlock` produces no audio; it snapshots the global params + module
  presence + playhead and calls `Engine::process`.
  - Holds the **canvas model**: `std::vector<ModuleInstance>` (module `id`,
    `type`, `x`, `y`). This lives on the processor, not the editor, so it
    survives the editor closing/reopening and rides along in DAW-project state.
  - `getStateInformation` / `setStateInformation` serialise the APVTS tree with
    the module layout as a `<Canvas>` child. This is project persistence, not
    preset Load/Save.
  - `refreshEngineConfig()` recomputes the four presence atoms
    (`hasArp/Random/Quantize/Shift`) after any model change. Position never
    affects DSP, so only *presence* is published.
- **APVTS parameters** (`ParamIDs`) — `root` (choice), `scale` (choice),
  `quantize` (bool), and `theme` (choice). The globals are real parameters now
  even though only the fixed-default engine consumes them; later per-module
  overrides will read the same values.

### Engine (the DSP)

- **`Engine`** (`Engine.*`) + **`ScaleTables`** (`ScaleTables.h`, header-only) —
  the Phase 2 MIDI engine. There is no user wiring yet, so the modules run as a
  **fixed implicit chain** with baked-in defaults; the full defaults and signal
  flow are documented at the top of `Engine.h`. In short:

  ```
  host MIDI → generators add notes (Arp, Random)
            → modulators transform pitch (Quantize, then Shift)
            → host output
  ```

  - Generators fire on a 1/16 grid and require the host transport to be playing.
  - On transport **stop**, every note the engine generated is released, matching
    the requirements' transport-boundary rule (nothing hangs).
  - With **no** modules present, MIDI passes through untouched.
  - `mapPitch` is the single pure function applied to both note-ons and
    note-offs, so an off can never mismatch its on.
  - When the Arp is active and playing it **swallows** the host notes (they are
    its input), so they don't also pass straight through.

  This fixed chain is a Phase 2 scaffold. It exists only so placed modules do
  *something* audible; it is not the real routing engine. When connections land,
  this is the piece that gets replaced by graph-driven processing.

### Editor and UI

- **`CurrentAudioProcessorEditor`** (`PluginEditor.*`) — derives from
  `juce::DragAndDropContainer` (it must be a common ancestor of both the palette
  and the canvas for drag-and-drop to work). Owns the shared
  `CurrentLookAndFeel`, hosts `MainView`, and provides two services to children:
  `showInlineDialog()` and `applyTheme()`.
- **`MainView`** (`MainView.*`) — pure layout: `MenuBar` on top, `Canvas` in the
  middle, `PaletteBar` along the bottom.
- **`MenuBar`** (`MenuBar.*`) — the global-settings combos (root, scale), the
  quantize toggle, and the Theme switch, each bound straight to the APVTS via
  attachments. The Edit and Load/Save menus from the requirements are deferred.
- **`Canvas`** (`Canvas.*`) — the `juce::DragAndDropTarget` drop surface and the
  bridge between the on-screen nodes and the processor model. It rebuilds its
  `ModuleComponent`s from the model, writes adds/moves/removes back, handles
  selection + Delete, and opens the settings placeholder on a node's
  double-click. Palette drags are recognised by a `"module:<type>"` tag in the
  drag description.
- **`ModuleComponent`** (`ModuleComponent.*`) — one draggable node. Generators
  paint as squares, modulators as circles (per the requirements' shape
  encoding); the fill comes from the theme's per-family colour. Ports are drawn
  as edge dots but are **decorative** — wiring them up is a later phase.
- **`PaletteBar`** (`PaletteBar.*`) — the tray of draggable chips, one per
  catalogue entry. Each chip mirrors the node's shape/colour so the user sees
  what they're about to place, and starts a drag carrying the module type.
- **`InlineDialog`** (`InlineDialog.*`) — the plugin's own in-hierarchy modal
  dialog (adapted from LAM). Used per the SnorkelAudioStandards rule that
  forbids `juce::AlertWindow` / `juce::DialogWindow`. In Phase 2 it backs the
  empty settings placeholder; its text-field plumbing is retained so later
  phases can grow real settings dialogs on the same base.

### Theming

- **`Theme`** (`Theme.h`, header-only) — the single source of truth for every
  paint-time colour. Two schemes (Light / Dark) following the SnorkelAudioStandards
  two-theme convention, plus per-family node fills (generator / modulator / I/O).
  Painting reads `CurrentTheme::active()` fresh every frame, so a theme swap is
  just "flip the active scheme + repaint".
- **`CurrentLookAndFeel`** (`CurrentLookAndFeel.h`, header-only) — one
  `LookAndFeel_V4` for the whole editor. `applyScheme()` pushes the active theme
  into JUCE's colour map so combos/buttons/menus pick it up without per-widget
  `setColour`. The editor calls it from its ctor and on every Theme change.

### The module catalogue

- **`ModuleTypes`** (`ModuleTypes.h`, header-only) — the catalogue the canvas
  knows about: the `ModuleKind` (Generator / Modulator / I/O) and `ModuleType`
  enums, the `ModuleDescriptor` (type, kind, name, family colour), and
  `moduleCatalogue()` — the four Phase 2 entries in display order. Stable
  string ids (`moduleTypeToString` / `FromString`) keep saved layouts valid
  across enum reorders.

  **This is the extension point.** A new module slots in by adding a
  `ModuleType`, a `moduleCatalogue()` entry, and its string mapping — plus a
  case in `refreshEngineConfig()` and its behaviour in `Engine`. Nothing else in
  the canvas/palette/UI needs to change.

## Deferred (out of scope for Phase 2)

Explicitly not built yet, listed here so a future session knows these are
intentional gaps rather than oversights:

- **Port wiring / connections.** The chain is implicit and fixed; ports are
  decorative. This is the big next piece and the reason the fixed-chain `Engine`
  is a placeholder.
- **User-editable per-module settings.** Modules run on baked-in defaults; the
  settings dialog is an empty placeholder.
- **Richer generator/modulator DSP**, and the rest of the module catalogue
  (I/O modules, the full generator/modulator lists from the requirements).
- **User preset Load/Save** (only DAW-project persistence exists).
- **Marquee/multi-select, group copy/paste/duplicate, pan, zoom, undo/redo.**
- **macOS / Windows / iPad targets** and the AU / AUv3 formats.
