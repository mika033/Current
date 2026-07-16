# Current — Module Reference

This file documents every module: the ones that exist today and the ones that
are planned. The first section is written in manual voice, intended to become
the user manual with light editorial passes as modules gain their real
settings. The planned sections are split into two tiers:

- **Speced** — the behaviour is concrete enough to be coded without further
  product decisions. Where a detail is still open it is flagged inline.
- **Idea-only** — the direction is set but real decisions are still missing;
  the entry records what is known and what must be decided before coding.

The tiers describe spec completeness, not scheduling. Where the requirements
mark a module as a later extension rather than v1, the entry says so.

## How modules work (applies to everything below)

Modules live on the canvas and come in three kinds. Generators create notes
and are drawn as squares. Modulators transform notes that flow through them
and are drawn as circles. I/O modules connect the graph to the outside world
and are drawn as triangles — MIDI In points right, Output points left.

Notes travel between modules as ordinary MIDI: pitch, velocity, channel, and
timing move together as one event, and what flows between ports is the same
shape as what an Output module finally sends to the host. Connections are made
port-to-port. One output may feed any number of inputs (fan-out), and several
outputs may feed one input (fan-in) — incoming notes simply merge.

Root, scale, and quantize have global settings in the menu bar. Modules use
the global values by default; a module may override them locally or ignore
them. Whether a module is deterministic or probabilistic is decided per
module — there is no global switch.

Generators share a set of common settings, applied where they make sense for
the specific module: a scale override (root plus scale type), a rate locked to
host tempo (e.g. 1/16), and an active-from/to bar range that limits the
generator to part of the arrangement.

Stateful time modules (Delay, Retrograde) follow shared transport rules: on
stop, everything stops and buffered material is discarded, with note-offs sent
so nothing hangs; on loop wrap, the buffer spills into the next pass; after a
playhead jump the buffer simply empties as normal.

## Implemented modules

The four generator/modulator modules below shipped with the Phase 2 canvas
skeleton and run with fixed default settings — double-clicking one opens a
placeholder where its settings will appear in a later phase. The two I/O
modules followed and are the first with a real setting: double-clicking them
opens a channel dialog. Until port wiring lands everything runs as one fixed
chain: host MIDI enters through MIDI In (or an implicit all-channels input if
none is placed), feeds the generators, results pass through Quantize and then
Shift, and exit through Output (or an implicit channel-preserving output).

### MIDI In (I/O)

MIDI In is where the outside world enters the graph: it brings in MIDI from
the host or an external source. Its one setting is the input channel — "All"
(the default) accepts everything, or pick a single channel 1–16 to listen to
just that channel; events on other channels are ignored entirely. Placing
several MIDI Ins listens to the union of their channels. With no MIDI In on
the canvas, the plugin behaves as if an all-channels MIDI In were present, so
nothing is required to get sound flowing.

### Output (I/O)

Output is where the graph leaves for the host. Its one setting is the MIDI
channel (1–16, default 1): every note and controller passing out is stamped
with that channel, which is how material gets routed to a specific synth on a
multitimbral track. Placing several Outputs sends a copy of the stream to each
channel. With no Output on the canvas, events keep whatever channel they
already had. Notes always release on the channel they started on, even if the
channel setting is changed while they sound — nothing hangs.

### Arp (generator)

The Arp turns notes you hold into a running arpeggio. Hold a chord — from your
keyboard or a clip — and while the transport is playing, the Arp steps through
the held notes one at a time. The held notes themselves are consumed: they are
the arpeggio's raw material and do not sound directly. When the transport
stops, so does the Arp, and anything sounding is released cleanly. When you
stop holding notes (or the transport is stopped), your playing passes through
unchanged, so live playing stays audible.

Current fixed behaviour: ascending order, 1/16-note rate, gate length of half
a step, velocity 100. Planned settings are the usual arp controls — direction,
octave range, rate, gate — plus the common generator settings.

### Random (generator)

Random plays notes drawn at random from the global root and scale, one per
step while the transport runs. It is a quick way to get evolving in-key
material to feed other modules.

Current fixed behaviour: notes are drawn between C3 and C5 (MIDI 48–72),
snapped into the global scale, at a 1/16-note rate with a gate of half a step
and velocity 100. Planned settings: the note range boundaries (per the
requirements, lower and upper limits) plus the common generator settings.

### Quantize (modulator)

Quantize forces every note passing through it onto a scale. Out-of-scale
pitches snap to the nearest scale member; in-scale pitches pass untouched. Use
it after anything chromatic — a Shift, an external keyboard — to keep the
result in key.

Current fixed behaviour: snaps to the global root and scale. Planned settings:
a local root/scale override. Note that the global Quantize toggle in the menu
bar applies the same snapping graph-wide; the module exists so quantization
can be placed at a specific point in the flow.

### Shift (modulator)

Shift transposes every note passing through it up or down by a fixed number
of semitones. Note-ons and note-offs shift together, so nothing ever hangs.

Current fixed behaviour: +12 semitones (one octave up). Planned settings: the
shift amount.

## Planned — speced

These are concrete enough to build. Each entry states the intended behaviour;
open details, where any, are flagged.

### Generators

- **LFO** — a traditional LFO (shape, rate, phase, depth) whose value is
  sampled on the generator's note grid and mapped to pitch across a
  configurable note range, emitting MIDI notes rather than a control signal.
- **Step Sequencer** — the user draws a fixed melodic pattern in a mini piano
  roll and the module plays it in a loop. The grid UI should follow the shared
  `design/grid-interaction.md` conventions.
- **Chord** — emits triads, 7ths, or other voicings instead of single notes.
  The core (pick a chord type, emit it on the generator grid from the current
  root) is codable now. The optional progression-walking feature still needs
  its data model and UI decided — how a user defines the progression.
- **Drone** — holds sustained notes indefinitely, re-triggering whenever the
  global root or scale changes. Which pitches it holds (root only, root plus
  fifth, a full chord) is its main setting; default to the root.

### Modulators — pitch

- **Harmonizer** — turns single notes into chords, with one selectable chord
  type. Octave-spreading (the classic octaver) is folded in as chord-type
  choices rather than a separate module.
- **Mirror / Invert** — reflects pitch around a configurable centre note:
  intervals above the centre become the same intervals below it, and vice
  versa.

### Modulators — time and rhythm

- **Delay / Echo** — repeats each note after a delay time, with feedback
  controlling the number of repeats and decay shaping their fading velocities.
  Stateful; follows the shared transport rules above.
- **Ratchet / Repeat** — subdivides or retriggers a note into a burst
  (settings: subdivision count and, likely, a velocity ramp).
- **Strum** — spreads the notes of a chord out over a short time window, like
  a strummed guitar (settings: spread time, direction).
- **Swing / Groove** — shifts off-beats later (or earlier) for groove. Follows
  the shared pair-based, loop-length-invariant swing model in the standards
  repo's `swing-timing.md`.
- **Note Length / Legato** — overrides or scales gate length, from staccato
  through fully legato.

### Modulators — rhythm application

- **Euclidean** — distributes N hits across M steps (the classic Euclidean
  rhythm) and imposes that timing on incoming pitch material: the input
  supplies pitch only, Euclidean supplies timing. On each hit it retriggers
  whichever pitches are currently held or active from the incoming stream —
  not a history buffer, not round-robin. If a chord is held, the whole held
  set retriggers together. This is the model for all rhythm-applying
  modulators: they override the input's own timing, unlike gating modules,
  which mask notes but keep their timing.

### Modulators — dynamics

- **Velocity Shaper** — modulates velocity with an LFO or a drawn curve
  (settings: shape, rate/length, depth).
- **Humanize** — adds small random timing and velocity jitter (settings: one
  amount each).

### Modulators — routing

- **Add** — two inputs, one output; combines all incoming notes. Functionally
  redundant with fan-in auto-summing, and kept deliberately: it gives users an
  explicit node to organize and label a merge point in the patch.
- **Split / Router** — sends notes to different outputs by pitch: e.g. notes
  above C3 to output A, below to output B. The pitch-threshold form is
  codable now; the requirements also allow splitting "by condition", whose
  vocabulary is still open.
- **Chance Branch** — routes each note to output A or B at random, with a
  probability setting.

### Modulators — filtering and gating

- **Rhythm** — a 16-step, one-lane on/off sequencer that decides which
  incoming notes pass and which are filtered out. Unlike the rhythm-applying
  modulators it keeps the surviving notes' own timing.
- **Probability Gate** (later extension) — each note passes with a set
  probability.
- **Range Filter** (later extension) — passes or rejects notes by note range.

## Planned — idea-only

Direction is set, but each needs real decisions before it can be coded. The
open questions are listed so specing them is a matter of answering, not
rediscovering.

### Modulators

- **Multiply / Divide** — makes incoming material slower or faster by a
  factor. Open: the semantics on a live stream — what the timing reference is
  (bar grid? first note?), how stretching interacts with notes that haven't
  finished, and what latency/buffering is acceptable.
- **Retrograde / Reverse** — buffers incoming material and plays it back
  reversed. Stateful; the shared transport rules apply. Open: the buffer
  window (a bar? a fixed time? fill-then-flip?), which drives everything else
  about it.
- **Note Limiter** (later extension) — caps polyphony. Open: the priority
  rule (newest wins? lowest? highest?) and whether displaced notes are cut or
  refused.
- **Conditional / Logic** (later extension) — passes notes based on musical
  conditions: first / not-first pass, fill, every-Nth-bar. Open: the condition
  vocabulary and how conditions combine.

### Generators (future tier in the requirements)

- **Random Walk / Brownian** — each note steps a bounded distance from the
  previous one, so pitch wanders instead of jumping.
- **Turing Machine** — looping shift-register randomness with a lock/lag
  knob; evolving but repeatable.
- **Markov / Probability Sequencer** — weighted transitions between notes or
  steps.
- **Cellular Automata** — Game of Life / Rule 110 mapped to a note grid,
  self-evolving.
- **Perlin / Smooth Noise** — continuous, non-periodic noise mapped to pitch.
