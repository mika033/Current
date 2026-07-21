# Current (working name): Generative MIDI Plugin Requirements

## Overview

A generative MIDI plugin. Formats: VST3 / AUv3. Targets: Mac and Windows (e.g. Ableton Live), and iPad (e.g. AUM).

The user has a canvas onto which they drag modules. Modules come in three kinds: **Generators**, **Modulators**, and **I/O modules** (MIDI In and Output).

## Global settings and design decisions

- **Pitch and rhythm travel together**: modules pass full note events (pitch and timing together), not separate pitch and trigger signals.
- **Note event format**: a note event is standard MIDI (note on/off with pitch, velocity, channel, timing) — no custom internal data format. The data flowing between module ports is the same shape as what an Output module sends to the host.
- **Global root / scale / quantize**: there are global root, scale, and quantize settings. By default, modules use the global settings. A module may ignore them or override them locally.
- **Deterministic vs probabilistic**: decided per module, not a global switch.
- **Rhythm-applying modulators**: a class of modulators takes existing melodic material and applies a rhythm to it, and may override the input's existing timing. This is distinct from gating (e.g. Rhythm), which masks notes but keeps their timing.
- **Ports**: modules expose explicit input and/or output ports, and connections are made port-to-port. Generators and MIDI In have outputs only; Output has an input only.
- **Fan-out and fan-in**: fan-out is always allowed (one output can feed multiple modules, e.g. the same source driving different modulators and then different synths). Fan-in is allowed; multiple outputs into one input merge, with MIDI notes added together.
- **Stateful modules at transport boundaries** (Delay, Retrograde):
  - **Stop**: everything stops. The buffer is not played out. Anything still sounding gets a note-off so nothing hangs.
  - **Loop wrap**: the buffer spills over into the next pass.
  - **Playhead jump**: edge case, not a priority. The buffer stays as-is and empties as normal. Fully musical results are not guaranteed.

## Generators

### Common settings

Applied wherever it makes sense for a given generator:

- **Scale**: two dropdowns. First sets the root (C, C#, ...), second sets the scale type (Major, Minor, ...).
- **Rate**: e.g. 1/16. Locked to host tempo/transport, except in Standalone mode (no host to sync to) — handled the same way Little Arp Monster (LAM) solves this.
- **Active from/to**: bar range in which the generator is active, e.g. only from bar 5 to 8. Uses the host's absolute bar count, following the same approach as LAM.

### Initial (v1)

- **Random**: user specifies a lower and upper note boundary.
- **Arp**: creates arpeggios, has the usual arp settings.
- **LFO**: all the settings of a traditional LFO, but outputs MIDI notes.
- **Step Sequencer**: user draws a fixed melodic pattern in a mini piano-roll.
- **Chord**: emits triads / 7ths / voicings instead of single notes, optionally walking a progression.
- **Drone**: holds sustained notes, re-triggering on scale/root change.

### Future

- **Random Walk / Brownian**: each note steps a bounded distance from the previous one, so it wanders instead of jumping.
- **Turing Machine**: looping shift-register randomness with a lock/lag knob, evolving but repeatable.
- **Markov / Probability Sequencer**: weighted transitions between notes or steps.
- **Cellular Automata**: Game of Life / Rule 110 mapped to a note grid, self-evolving.
- **Perlin / Smooth Noise**: continuous, non-periodic noise mapped to pitch.

## Modulators

All modulators listed here are initial (v1), except the few marked as possible later extensions.

### Pitch

- **Quantize / Scale-Snap**: forces incoming notes onto a scale.
- **Harmonizer**: turns single notes into chords, with one selectable chord type. Octaver behavior (spreading notes across octaves) is folded into this module.
- **Mirror / Invert**: reflect pitch around a center note.
- **Shift**: shift notes up or down.

### Time / rhythm

- **Multiply / Divide**: make incoming notes slower or faster.
- **Delay / Echo**: repeats notes with feedback and decay.
- **Ratchet / Repeat**: subdivides or retriggers a note into a burst.
- **Strum**: spreads chord notes over time.
- **Swing / Groove**: shifts off-beats.
- **Note Length / Legato**: modulates gate length.
- **Retrograde / Reverse**: buffers and plays back reversed (stateful).

### Rhythm application

Modulators that impose a rhythm on incoming melodic material, overriding the input's own timing (see Global settings and design decisions).

- **Euclidean**: distributes N hits across M steps and applies that pattern to incoming pitch material. Pitch is fed from another module; Euclidean supplies timing only. On each hit, it retriggers whichever pitch(es) are currently held/active from the incoming stream (not a history buffer, not round-robin cycling); if multiple notes are held (a chord), the whole held set is retriggered together.

### Dynamics

- **Velocity Shaper**: LFO or curve on velocity.
- **Humanize**: small random timing and velocity jitter.

### Routing

- **Add**: two inputs, combines all incoming notes into one output. Kept despite fan-in auto-summing making it functionally redundant: it gives users an explicit node to organize/label a merge point in the patch.
- **Split / Router**: send notes to different outputs by pitch or condition, e.g. notes above C3 to one output, below to another.
- **Chance Branch**: probabilistically route to output A or B.

### Filtering / gating

- **Rhythm**: 16-step, one-lane on/off sequencer that specifies which notes pass through and which get filtered out.

Possible extensions (later):

- **Probability Gate**: each note passes with a set probability.
- **Range Filter**: pass or reject by note range.
- **Note Limiter**: cap polyphony with a priority rule.
- **Conditional / Logic**: first / not-first, fill, every-Nth-bar conditions.

## I/O modules

The graph's endpoints. Both use ports like any other module.

- **MIDI In**: brings MIDI from the host or an external source into the graph. Source module (output port only), like a generator. At least one is present. (This is the former External Input generator.)
- **Output**: sends MIDI to the host, assignable to a MIDI channel. Sink module (input port only), the only module type with no output. Multiple Output modules can exist, each routing to a different channel or destination synth.

## UI

### Canvas

- A central canvas where the user drops modules.
- Modules are connected port-to-port with flow arrows that show signal direction.
- Modules can be freely moved around by the user.
- Generators are drawn as squares, modulators as circles. I/O modules are triangles: Input points right, Output points left.
- Color encodes module family (pitch / time / dynamics / routing), not a unique color per module. A symbol identifies the specific module within its family.

### Interaction

Gestures are shared between desktop and touch where possible:

- **Select**: click / tap a module or a connection.
- **Multi-select (marquee)**: drag on the empty canvas background selects all modules within the dragged rectangle. Supports group Copy / Paste / Duplicate.
- **Move a module**: drag its body.
- **Open settings**: double-click / double-tap a module.
- **Connect**: drag from an output port to an input port. On touch, ports have enlarged hit targets, with a tap-output-then-tap-input alternative.
- **Delete**: right-click or long-press for a context menu (delete, duplicate). On desktop, select and press Delete.
- **Pan**: secondary gesture, since empty-canvas drag is marquee select. Two-finger drag on touch; space+drag (or middle-click drag) on desktop.
- **Zoom**: deferred for now.

### Module palette

- Below the canvas: the set of modules that can be dragged onto it. On smaller (iPad) screens it is a scrollable, collapsible tray.

### Menu bar (above the canvas)

- Global settings: root, scale, quantize.
- Edit: Copy, Paste, Duplicate. Undo / Redo deferred to a later phase, not needed initially.
- Load / Save: user presets, plus automatic persistence of state within the DAW project — both, following the shared `preset-system-guideline.md` convention (identity, storage locations, manager API) as already implemented in LAM.

## Open items

None currently — all resolved above.

## Phases

### Phase 1: Basics

- Build scripts copied from an existing Snorkel plugin (e.g. LAM) and adjusted for Current, per the shared `build-scripts.md` naming/roles convention.
- A stub app builds and runs as VST3, AU, and Standalone, showing an empty UI window.
- Basic UI layout is in place: canvas area, menu bar, and a module section (palette). Canvas and module section are empty placeholders.
- Menu bar is empty except for a Light/Dark theme toggle button.
- Light/Dark theme switching works, following the shared `design/themes.md` palette and painting rules.

### Phase 2: Canvas skeleton

- Canvas is functional: modules can be dragged in from the palette and moved around.
- Palette offers two generators and two modulators only (specific modules to be chosen).
- Global root / scale and quantize settings exist.
- No Load / Save.
- The four modules run with fixed default settings that the user cannot change yet.
- Double-clicking a module opens an empty settings window, a placeholder where the module's settings will later be added.

### Later phases

Phase 3 onward will be scoped after testing the results of Phase 2, not defined upfront.
