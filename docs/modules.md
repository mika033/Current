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

Root and scale have global settings in the menu bar. Modules use the global
values by default; a module may override them locally or ignore them. (There
is no global quantize switch — snapping to a scale is the Scale modulator's
job, placed where it's wanted in the flow.) Whether a module is deterministic
or probabilistic is decided per module — there is no global switch.

Stateful time modules (Delay, Retrograde) follow shared transport rules: on
stop, everything stops and buffered material is discarded, with note-offs sent
so nothing hangs; on loop wrap, the buffer spills into the next pass; after a
playhead jump the buffer simply empties as normal.

Settings changes apply **live**: while a module's settings window is open,
every dial turn and combo pick takes effect (and is audible) immediately. OK
keeps what you hear; Cancel (or Esc, or clicking outside the window) restores
the settings as they were when the window opened.

## Shared settings

Some settings recur across many modules. The governing rule is that a shared
setting **behaves identically wherever it appears** — the same control, the
same option list, the same meaning — so the user learns it once and always
knows what to expect. Each shared setting is meant to be one piece of code
that every module references, not a per-module reimplementation. (Several of
the points below are the *agreed* model that a later session rolls out
module-by-module alongside the `ModuleWindow` conversion; where today's code
still differs it is being brought into line, not left as an exception. See the
decision log in `CLAUDE.md`.)

- **Root and scale — always a pair.** Every module that maps pitch carries
  *both* a Root and a Scale override — never one without the other. Each
  offers the same three kinds of choice: **Global** (the default — track the
  menu bar live, so a module left on Global follows later menu-bar changes),
  **Off** (do not force the module's output onto a scale), or a **named**
  value (root C to B, scale from the menu-bar list). "Off" reads according to
  what the module does with pitch: on Shift and on the Delay's per-echo shift,
  Scale = Off means the move is in raw semitones, while Scale on (Global or
  named) means the move is in scale steps. Used by the Random, Scale, and LFO
  generators, the Chord and Drone generators, and the Scale, Progression,
  Shift, and Delay modulators. Modules that don't touch pitch (Arp, Rhythmize,
  Quantize, MIDI In, Output) carry neither.
- **Rate — with Gate on note-emitting modules.** Rate is the note-length grid
  a module works on, **1/32 to 1/1**, locked to host tempo (the same canonical
  list everywhere Rate appears): the grid a generator emits on, or the grid a
  modulator re-times passing MIDI to. A module has **either Rate or Length, or
  neither — never both.** On the note-emitting Rate modules — the Random,
  Scale, and LFO generators and the Arp and Rhythmize — Rate always ships
  **paired with Gate** (below). Quantize uses a Rate as its timing grid and Delay uses one
  as its echo spacing, but neither has a Gate: they re-time or echo existing
  notes rather than originating durations, so a gate would have nothing to act
  on.
- **Gate** — how long each *emitted* note sounds, as a percent of its step
  (1–100% in 1% steps, default 50%). Ships on every note-emitting Rate module
  (the generators, the Arp, and Rhythmize); it does not appear on Quantize or
  Delay.
- **Length** — a single held note or chord, for the slow generators that
  naturally produce one long note (Chord, Drone). Drawn from the shared
  bar-length list (1/4 bar to 16 bars). A Length module has no Gate — the user
  shortens the note simply by choosing a shorter Length.
- **Repeat — one meaning everywhere.** Repeat is the period after which a
  module **restarts from its start**, regardless of where it currently is: the
  scale walk jumps back to step 1, the arp restarts its pattern, a held chord
  re-triggers. It is counted from the song's bar 0 (so the pattern sits
  identically on every host loop pass), drawn from the shared bar-length list
  plus an **Endless** entry (never restart — the usual default for stepped
  emitters). Repeat is independent of Rate/Length and may sit alongside either
  or be absent (Random and LFO have none — Random is stochastic, the LFO is
  cyclic by its own Cycle length). On a Length module the two combine
  literally: a Chord with Length 2 bars and Repeat 4 bars sounds for 2 bars,
  then rests for 2 bars (the `Repeat − Length` remainder is silence), then
  re-triggers; Length equal to or above Repeat plays legato back-to-back.
- **Mode** (Up, Down, …) and **Octaves** — pattern direction and octave span,
  shared by the modules that walk a pattern (Arp, Scale; Scale offers Up and
  Down only). "Octaves" here (a 1–4 range span) is a different concept from
  the per-module "Octave" transpose offset (−2 to +2, on Drone and on each
  Progression step); both words are kept because each is the conventional term
  for its concept, and the module-window label readout makes the meaning
  unambiguous (`Octaves: 2` spans two octaves; `Octave: +1` shifts up one).
- **Bar-length list** — the single canonical list of bar durations (1/4 bar
  to 16 bars) that every bar-based control draws from: Length, Repeat (plus
  Endless), the LFO's Cycle length, and the Progression's **Length** (its
  per-step duration, drawn from this same list so that "Rate" stays reserved for
  the 1/32–1/1 note grid).

Also planned as a shared setting: an active-from/to bar range that limits a
module to part of the arrangement (not yet implemented anywhere).

## Implemented modules

The first modules shipped with the Phase 2 canvas skeleton running fixed
default settings; the two I/O modules followed with the first real setting
(their channel dialog), the Random and Scale generators gained full settings
dialogs, and the Arp — reclassified as a modulator, since it transforms the
notes flowing into it — now carries one too. Shift gained its settings
(amount + scale), a third generator, the LFO, arrived with a full dialog, and
the Delay — the first stateful time modulator — followed. The latest batch
reworked Quantize into a timing quantizer (rate + swing; the old global
Quantize checkbox is gone) and added the Scale and Progression modulators, so
every module in the palette now has real settings. The slow generators —
Chord and Drone — arrived next, complete with settings. **Port wiring is now
real**: modules are connected by dragging a cable from one module's output
port to another's input port, and the cables *are* the signal flow. Host MIDI
enters the graph only through MIDI In modules, flows along the cables through
whatever the user wired up, and leaves only through Output modules — there is
no hidden routing, so an unwired module is silent and a patch with no Output
makes no sound. One output can feed several modules (fan-out) and several
outputs can merge into one input (fan-in). A fresh instance starts with a
wired MIDI In → Output pass-through as a working example. Every placed module
is fully independent — two Arps in different branches run with their own
settings and their own state. Rewiring the patch (adding or removing modules
or cables) cuts any sounding notes; editing a module's settings does not.

Each implemented module's entry ends with a "User settings" bullet list of
what the user can change today, so the gap between current and planned
settings is always explicit.

### MIDI In (I/O)

MIDI In is where the outside world enters the graph: it brings in MIDI from
the host or an external source. It is the only door in — host MIDI reaches a
module only by being wired (directly or through others) to a MIDI In. Its one
setting is the input channel — "All" (the default) accepts everything, or pick
a single channel 1–16 to listen to just that channel; events on other channels
are ignored entirely. Wiring several MIDI Ins into the same module merges
their channels. A note always releases cleanly even if the channel setting is
changed while it sounds.

User settings:

- Channel — All (default) or 1 to 16.

### Output (I/O)

Output is where the graph leaves for the host — the only door out. Whatever
is wired into an Output is what the plugin plays; a patch with no Output (or
with material not routed to one) is silent. Its one setting is the MIDI
channel (1–16, default 1): every note and controller passing out is stamped
with that channel, which is how material gets routed to a specific synth on a
multitimbral track. Wiring the same source into several Outputs sends a copy
of the stream to each channel. Notes always release on the channel they
started on, even if the channel setting is changed while they sound — nothing
hangs.

User settings:

- Channel — 1 to 16 (default 1).

### Random (generator)

Random plays notes drawn at random from a root and scale, one per step while
the transport runs. It is a quick way to get evolving in-key material to feed
other modules. Each step picks uniformly from the pitches of the selected
scale that lie inside the note range; the gate is half a step, velocity 100.

Root and scale each default to Global — the module follows the menu bar, and
tracks later menu-bar changes — or can be set locally (root C to B, the same
scale list as the menu bar). The rate sets the step length from 1/32 to 1/1.
The range is a pair of MIDI notes (inclusive); when the module is dropped it
defaults to the root at octave 2 up to the root at octave 4 (C2 to C4 for a C
root). If the range boundaries end up reversed the module swaps them, and if
the range contains no scale note at all it snaps to the nearest one rather
than fall silent. The node shows its rate as a sublabel.

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default), Off (draw from all twelve chromatic notes), or any
  scale from the global list.
- Rate — 1/32 to 1/1 (default 1/8).
- Gate — 1% to 100% of the step in 1% steps (default 50%).
- Range from / to — any MIDI note (default root octave 2 to root octave 4).

### Scale (generator)

Scale walks a scale stepwise, one note per step while the transport runs — an
instant scale-run pattern. The walk starts at the root in octave 3 (C3 for a C
root), spans the set number of octaves, and either climbs (Up) or descends
(Down — the same notes played top-to-bottom). "End on" chooses how the run
closes: Root (octave) appends the octave root as a final step — the classic
eight-note scale run — while 7th stops on the scale's last degree (for a
pentatonic that is its 5th degree; the option keeps the "7th" name from the
common seven-note case). Root and scale each default to Global, like Random.

The rate sets the step length; Repeat sets when the pattern restarts, counted
from the song's bar 0 (1/4 to 4 bars, assuming 4/4). A pattern longer than the
repeat window is cut off mid-run; a shorter one rests until the window comes
round. On Endless there is no window: the pattern loops back-to-back,
restarting right after its last note. The defaults line up deliberately: 1/8
steps, one octave, End on Root, repeat every bar — an eight-note run filling
exactly one bar (Repeat's usual default is Endless; this module is the
deliberate exception). Velocity is 100, and the node shows its rate as a
sublabel. Scale offers no Off — a scale-walking generator needs a scale to walk.

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default) or any scale from the global list.
- Mode — Up (default) or Down.
- Octaves — 1 (default) to 4.
- End on — Root (octave, default) or 7th.
- Rate — 1/32 to 1/1 (default 1/8).
- Gate — 1% to 100% of the step in 1% steps (default 50%).
- Repeat — Endless, 1/4 bar, 1/2 bar, 1 bar (default), 2, 4, 8, or 16 bars.

### LFO (generator)

The LFO turns a classic low-frequency oscillator into melody: its value is
sampled on a note grid and mapped to pitch, so instead of a control signal
you get a stream of notes tracing the shape. The cycle length sets how long
one full sweep of the shape takes, in bars (1/4 bar to 16 bars, default 1
bar), anchored to the song's bar 0. The rate sets how many notes are output —
one per step, 1/32 to 1/1 (default 1/16). Depth sets how far the pitch
swings around the centre note (the root at octave 3, so C3 for a C root),
given as whole octaves (0–4, default 1) plus extra scale steps (0–6, default
0) — both directions, so a depth of one octave sweeps C2 to C4. All movement
is in scale degrees, so the result always lands in key.

Shape offers the usual suspects: Sine (default), Triangle, Saw Up, Saw Down,
Square, and Random, which redraws a fresh value for every note instead of
tracing the cycle. Phase sets where in the cycle playback starts — 0, 90,
180, or 270 degrees — useful for offsetting two LFOs against each other or
starting a sine at its peak. Root and scale default to Global like the other
generators; Scale = Off maps the shape chromatically instead of in scale
degrees. Velocity is 100, and the node shows its rate as a sublabel.

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default), Off (map chromatically), or any scale from the
  global list.
- Shape — Sine (default), Triangle, Saw Up, Saw Down, Square, or Random.
- Cycle length — 1/4 bar to 16 bars (default 1 bar).
- Depth (octaves) — 0 to 4 (default 1).
- Depth (scale steps) — 0 to 6 (default 0).
- Rate — 1/32 to 1/1 (default 1/16).
- Phase — 0 (default), 90, 180, or 270 degrees.

### Chord (generator)

The Chord emits whole chords instead of single notes: a stack of scale
degrees built on a chosen degree of its root and scale, so every voicing
stays diatonic wherever it sits (in C major, a 7th on V is G–B–D–F). Degree
picks where the chord is built (I–VII, default I); Type picks the stack —
Triad (default), 7th, Sus2, Sus4, 5th (the two-note power chord), or 6th;
Inversion (Root, 1st, 2nd) lifts that many of the lowest tones an octave.
The chord is voiced from the shared generator centre, the root at octave 3.

Pacing uses the bar-based Length/Repeat pair (see Shared settings): a new
chord starts every Repeat and sounds for Length, both defaulting to 1 bar —
back-to-back chords out of the box. Chord tones run through the modulator
chain like any generated notes, so a Progression walks the chord through its
steps, and Quantize and the Delay treat the tones as ordinary notes. To hear
chord changes, pair a Chord with a Progression: the Chord supplies the
voicing, the Progression the harmonic movement.

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default) or any scale from the global list.
- Degree — I (default) to VII; the scale degree the chord is built on.
- Type — Triad (default), 7th, Sus2, Sus4, 5th, or 6th.
- Inversion — Root (default), 1st, or 2nd.
- Length — 1/4 bar to 16 bars (default 1 bar); how long the chord sounds.
- Repeat — Endless, 1/4 bar to 16 bars (default 1 bar); how often a new chord
  starts (Endless sounds it once, continuously back-to-back).

### Drone (generator)

The Drone holds long sustained pitches under everything else. Voicing picks
what it holds — Root (default), Root+5th (the perfect fifth snapped into the
scale, so e.g. Locrian holds its diminished fifth), Root+Octave, or Triad
(scale degrees, like the Chord) — and Octave moves the whole voicing up to
two octaves either way from the root-at-octave-3 centre.

Its defining behaviour is the immediate re-trigger: whenever the pitches it
should be holding change mid-hold — the root or scale is edited, the voicing
or octave changes — the old notes are released and the new ones start at
once, keeping the remainder of the hold time, instead of waiting for the next
window. Pacing is the bar-based Length/Repeat pair, both defaulting to 4 bars
(drones move slower than chords). Routing is the user's choice like any
module, but a drone is continuous material: wiring it through Quantize or a
Delay rarely helps (its starts sit on bar boundaries already, and echoing a
held pad produces blips, not echoes) — a Drone → Output branch of its own is
the natural patch. (A Drone wired through a Progression holds its pitch
across a step change and picks the new degree up at its next window; making
held notes follow a step change mid-hold is a possible later Progression
feature.)

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default) or any scale from the global list.
- Voicing — Root (default), Root+5th, Root+Octave, or Triad.
- Octave — -2 to +2 (default 0), around the root at octave 3.
- Length — 1/4 bar to 16 bars (default 4 bars); how long the hold sounds.
- Repeat — Endless, 1/4 bar to 16 bars (default 4 bars); how often a new hold
  starts (Endless holds continuously back-to-back).

### Arp (modulator)

The Arp re-times what flows into it, turning held notes into a running
arpeggio. Hold a chord — from your keyboard or a clip — and while the
transport is playing, the Arp steps through the held notes one at a time on
its rate grid. The held notes themselves are consumed: they are the
arpeggio's raw material and do not sound directly. Mode sets the walk — Up,
Down, Up-Down (turning points not doubled), or Random — and Octaves extends
it across up to four octaves, repeating the held pattern an octave higher
each pass. Gate sets how long each note sounds as a share of its step.
Repeat, normally Endless, resets the walk to its start at every window
boundary, snapping the arpeggio back into phrase-length phrases.

When the transport stops, so does the Arp, and anything sounding is released
cleanly. When you stop holding notes (or the transport is stopped), your
playing passes through unchanged, so live playing stays audible. Velocity is
fixed at 100 for now, and the node shows its rate as a sublabel.

User settings:

- Mode — Up (default), Down, Up-Down, or Random.
- Rate — 1/32 to 1/1 (default 1/16).
- Octaves — 1 (default) to 4.
- Gate — 1% to 100% of the step in 1% steps (default 50%).
- Repeat — Endless (default), 1/4 bar, 1/2 bar, 1 bar, 2, 4, 8, or 16 bars.

### Rhythmize (modulator)

Rhythmize imposes a rhythm on whatever flows into it: a 16-step on/off
pattern, shown in its settings window as two rows of eight step boxes (styled
after Little Arp Monster's pattern grid). Hold a chord — or feed it sustained
material from a generator — and while the transport plays, every **active**
step retriggers the whole currently-held set as fresh notes of the step's
length; inactive steps stay silent. Pitches are never changed: the input
supplies pitch, Rhythmize supplies timing (the rhythm-application model the
planned Euclidean module describes, driven by a hand-set pattern instead of a
formula). Velocities are kept too, so a softly played chord retriggers softly.

The pattern follows the song position — step = grid index mod 16, counted from
the song's start — so at the default 1/16 rate the 16 steps are exactly one
bar and the pattern lands identically on every host loop pass. All 16 steps
start active: drop the module in and everything retriggers in straight 16ths;
carve the groove by switching steps off. Input notes are consumed while
playing (they are the module's data); like the Arp, when the transport is
stopped input passes through unchanged so live playing stays audible. An
emitted note rings to its gate end even if the key lifts mid-step, Gate sets
that length as a share of the step, and a transport stop releases everything
cleanly. While the transport runs, a glowing halo in the settings window marks
the step currently playing (it follows the song position, so it also shows
where the pattern sits while you edit); it disappears when the transport
stops. The node shows its rate as a sublabel.

User settings:

- Rate — 1/32 to 1/1 (default 1/16); the step grid.
- Gate — 1% to 100% of the step in 1% steps (default 50%).
- Steps — 16 on/off boxes (all on by default), two rows of eight in the
  settings window.

### Quantize (modulator)

Quantize is about timing, not pitch: while the transport runs, every note
flowing through is moved onto its rate grid — a note played (or generated)
between grid points waits for the next one. Use it to tighten loose live
playing, or to put groove onto straight material via swing. A note released
while it is still waiting keeps its played duration; a note already on the
grid passes unmoved. When the transport is stopped there is no grid, so
everything passes straight through and live playing stays immediate.

Swing (0–100%, default 0) pushes every second grid step late, following the
shared pair-based model from the standards repo's `swing-timing.md` (the same
maths as Little Arp Monster): within each pair of steps the first stretches
by swing/2 of a step and the second shrinks by the same amount, so pair
starts always sit on the straight grid and the loop length never changes.
Around 67% gives the classic triplet shuffle. A generator running at the
Quantize rate lands exactly on the swung grid — instant shuffle for the Scale
generator's runs. As a stateful time module, Quantize follows the shared
transport rules: on stop, notes still waiting are discarded and nothing
hangs. The node shows its rate as a sublabel.

User settings:

- Rate — 1/32 to 1/1 (default 1/16); the timing grid.
- Swing — 0% to 100% in 1% steps (default 0%).

### Scale (modulator)

The Scale modulator forces every note passing through it onto a scale:
out-of-scale pitches snap to the nearest scale member, in-scale pitches pass
untouched. Use it after anything chromatic — a Shift set to semitones, an
external keyboard — to keep the result in key, placed exactly where you want
the snapping to happen. Root and scale each default to Global, following the
menu bar like the generators; the node shows the chosen scale as a sublabel.
(Not to be confused with the Scale generator, the square one — that plays
scale runs; this one corrals what flows through it.)

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default) or any scale from the global list.

### Progression (modulator)

Progression turns a static loop into a chord progression: it transposes
everything flowing through it to the current step of a step list you define —
hold a C-major vamp, give the module I-IV-V, and the vamp walks the changes
by itself. Each step is a scale degree (I to VII) plus an octave offset (−2
to +2); degree movement is diatonic (in scale members of its root/scale, so
the result stays in key), the octave offset is plain ±12s. Degree I with
octave 0 — the default step — passes notes untouched.

Length sets how long one step lasts (1/4 bar to 16 bars, default 1 bar), anchored
to the song's bar 0; the list loops when it runs out. The settings window shows
the steps as a row of cells (degree big, octave as a corner tag); a trailing
cell's up/down arrows add a step or remove the last one (1 to 8 steps), and
clicking a cell selects it so the Degree and Octave menus below edit it. Root and
scale default to Global. While the
transport is stopped the first step applies, so auditioning matches how
playback will start. Note-offs always release what their note-on sounded,
even across a step change — nothing hangs mid-chord-change. The node shows
short progressions in full ("I-IV-V") and long ones as a step count.

User settings:

- Root — Global (default) or C to B.
- Scale — Global (default), Off (walk degrees chromatically), or any scale
  from the global list.
- Length — 1/4 bar to 16 bars (default 1 bar); the length of one step.
- Steps — 1 to 8 steps (default one step, I); per step a degree I–VII
  (default I) and an octave −2 to +2 (default 0).

### Shift (modulator)

Shift transposes every note passing through it up or down by a set amount
(−36 to +36, default 0 — a fresh Shift passes notes through untouched until
you dial it in). What the amount means depends on the scale setting. With a
scale active — Global (the default, following the menu bar) or any named
scale — the shift moves in scale steps: +2 in C major turns C into E, and
out-of-scale notes snap into the scale as part of the walk, so the result is
always in key. With the scale set to Off, the shift is plain chromatic
semitones: +2 turns C into D regardless of key. Root sets the reference the
scale-step walk counts from (Global by default, following the menu bar); it is
ignored when the scale is Off. Note-ons and note-offs shift together, so nothing
ever hangs, and the node shows its signed amount as a sublabel.

User settings:

- Root — Global (default) or C to B; the reference for scale-step shifts.
- Scale — Global (default), Off (chromatic semitones), or any scale from the
  global list.
- Amount — −36 to +36 (default 0); scale steps or semitones per the scale
  setting.

### Mirror (modulator)

Mirror turns a line upside-down: it reflects every note around a centre note,
so an interval above the centre comes out the same interval below (with the
centre at C4, E4 a third up becomes A♭3 a third down). Rhythm and phrasing are
untouched — only pitch flips. Like Shift, what the reflection means follows the
scale: with a scale active — Global (the default) or any named scale — it mirrors
in scale degrees, so E4 in C major folds to A3 and the result stays in key; with
the scale set to Off it mirrors in chromatic semitones (E4 → A♭3). The centre
control has an Off position at the far-left of its dial; there the inversion is
skipped and only the register window (below) acts, so Mirror doubles as a plain
range tool.

After the inversion, the result is constrained to a **register window** between
a Low and a High note — the two can't cross (turning one past the other pushes
it along), and setting them equal makes a one-note window. The Bounds control
decides what happens to a note that lands outside the window: **Limit** drops it
(it simply doesn't sound), while **Mirror** folds it back once across the nearest
edge — a note below Low bounces up above Low by the same distance — staying in the
window's domain (scale degrees with a scale active, semitones with Off) so it
stays in key. A fold that would still overshoot (a note more than a window-width
out) is clamped to the edge, so the window is always honoured. A dropped or folded
note never hangs: note-ons and note-offs map the same way, and a note Limit drops
was never emitted, so it books no dangling note-off. The node shows the centre
note (or the Low–High window when the centre is Off).

User settings:

- Root — Global (default) or C to B; the reference for the scale-degree mirror.
- Scale — Global (default), Off (chromatic mirror), or any scale from the global
  list. Governs both the inversion and the boundary fold, so the module is never
  half-tonal.
- Centre — Off or any MIDI note (default C4); the note the inversion reflects
  around, Off = no inversion.
- Low / High — the register window's bounds (defaults C2 / C6); Low can't exceed
  High.
- Bounds — Limit (drop out-of-window notes) or Mirror (fold them back inside);
  default Mirror, so nothing is silently dropped out of the box.

### Harmonizer (modulator)

Harmonizer turns each note you play into a chord: it takes the played note as
the bass and stacks extra voices on top, so a single-finger line comes out as
block chords. It is the Chord generator's voicing brain, but driven by what you
play instead of a fixed degree — where the Chord generator asks "which degree of
the key?", the Harmonizer just harmonises whatever note arrives. It is purely
additive and rides the input's own timing: every added voice starts and stops
with the note that spawned it, so there is no window, no rate, and no added
latency.

The stack stays diatonic to the module's Root/Scale — the played note is read as
a scale degree and the added voices are the scale tones above it (play E in C
major → E–G–B). With **Scale = Off** it stacks fixed chromatic intervals instead
(a Triad is always a major triad in semitones), so it harmonises anything
regardless of key. Type picks the shape — Triad (default), 7th, Sus2, Sus4, 5th,
6th, plus **Octave** and **Octave+5th** (the classic octaver folded in as chord
types) — and Inversion re-voices the harmony, lifting its lowest voices an octave
while the played note stays the bass. Each added voice runs through the rest of
the pitch chain (Scale, Progression, Shift, Mirror) and Quantize/Delay exactly
like the played note, so downstream modules see the whole chord.

**Mode** decides what happens when you hold more than one note at once:

- **Add** (default) — every held note gets its own stack. Play a chord in and
  each note is harmonised independently: the way to thicken a pad or voice block
  chords. Polyphony climbs fast (a four-note chord on a Triad is twelve voices).
- **Replace** — only the newest note is harmonised; playing a new note cuts the
  previous note and its whole stack. A monophonic harmoniser — the clean choice
  for a single melodic line. (No fall-back to still-held older notes: releasing
  the current note simply stops it.)
- **Top** — only the highest held note is harmonised; everything below it passes
  through untouched. Hold a chord and play a melody above it and only the melody
  grows the harmony. Releasing the top note re-harmonises whatever note is now
  highest.

A Harmonizer stacks voices on whatever is wired into it — played input, a
generator's stream, or an Arp's output all work (wire a Random generator in
and every drawn note becomes a chord). The added voices ride their source
note's own note-on and note-off, so removing the module or stopping the
transport never leaves one hanging. The node shows the chord Type.

User settings:

- Root — Global (default) or C to B; the reference for the diatonic stack.
- Scale — Global (default), Off (chromatic stacking), or any scale from the
  global list.
- Type — Triad (default), 7th, Sus2, Sus4, 5th, 6th, Octave, or Octave+5th.
- Inversion — Root (default), 1st, or 2nd; lifts the lowest voices an octave.
- Mode — Add (default), Replace, or Top; how simultaneous held notes are treated.

### Delay (modulator)

Delay repeats every note that passes it as a fading echo chain — the classic
tape-echo feel, in MIDI. Each note (played or generated) spawns a repeat one
delay time later; Rate sets that spacing as a note length (1/32 to 1/1,
default 1/8), locked to host tempo. Feedback (0–90%, default 50%) sets each
repeat's velocity as a share of the note before it; the chain ends when the
repeats fade below audibility, so feedback doubles as the number of repeats —
50% gives four, lower gives fewer, higher gives a long tail.

Shift (−12 to +12, default 0) transposes each individual repeat by that amount
relative to the repeat before it, so the chain climbs (or falls) as it fades —
+12 turns an echo into an ascending cascade. Like the Shift modulator, the unit
follows the scale: with a scale active (Global or named) the shift moves in
scale degrees so the cascade stays in key, and with the scale Off it moves in
chromatic semitones (+12 = an octave). A repeat that would leave the MIDI note
range ends the chain rather than piling up at the edge. Echoes keep their
note's channel, sound for half the delay time each, and follow the shared
transport rules: on stop, pending repeats are discarded and sounding ones
released, so nothing hangs; echoes also work while the transport is stopped, so
live playing echoes too.

User settings:

- Root — Global (default) or C to B; the reference for scale-step shifts.
- Scale — Global (default), Off (chromatic semitones), or any scale from the
  global list.
- Rate — 1/32 to 1/1 (default 1/8); the echo spacing.
- Feedback — 0% to 90% in 1% steps (default 50%); repeat decay, and thereby
  repeat count (0% = no echoes; capped at 90% so the tail always ends).
- Shift — −12 to +12 (default 0); applied to each repeat, cumulatively across
  the chain (scale steps or semitones per the scale setting).

### Strum (modulator)

Strum spreads the notes of a chord out over a short time window, like a
strummed guitar: notes that arrive together are released one after another,
fanning out instead of hitting at once. It re-times and re-shapes the chord's
own notes and maps no pitch, so it carries no Root/Scale. Like every timing
modulator here it can only delay (a real-time MIDI effect can't play a note
earlier than it arrived), so the chord fans *late*. Which notes count as one
chord is decided automatically by a small fixed detection window — no user
control — and that window is the small latency the strum costs, since the fan
order can't be chosen until the whole chord has arrived.

Its six controls fill the settings grid over a blank menu bar. Direction and
Repeat are the shared controls, so they read identically to the rest of the
plugin.

- **Spread** sets the gap between consecutive fanned notes, tempo-synced: the
  dial runs from Off (the chord hits together — an effective bypass) through a
  1/16 gap at the middle to an 1/8 gap at the top, and in between the readout
  shows the gap in milliseconds at the current tempo. The gap is per note, not
  a total: a 3-note chord at 1/16 lands at 0, +1/16, +2/16, so bigger chords
  fan longer. Tempo-synced so a strummed comp keeps its feel when the song
  tempo moves.
- **Direction** is the shared Mode control: Up (low to high, a downstroke),
  Down (high to low, an upstroke), Up-Down (alternating strokes on successive
  strums, the way a real player alternates), or Random (shuffled note order).
- **Curve** shapes the inter-note spacing across the fan: Even (equal gaps),
  Accelerate (the gaps shrink toward the end, so the notes bunch up late — the
  natural pick-stroke feel), or Decelerate (the reverse).
- **Velocity** tilts loudness across the strum, −100% to +100% (default 0):
  negative starts loud and fades away (a bass-note accent), positive swells up,
  0 is flat.
- **Jitter** adds looseness — a small random variation in each note's timing
  (and a touch of velocity) so repeated strums don't land identically. It is
  drawn from the song position, not a free-running dice roll, so a looped part
  strums the same way on every pass instead of shimmering.
- **Repeat** is the shared Repeat control (Endless plus bar lengths to 16).
  Endless strums the held chord once; a bar length re-strums it every that
  period, turning Strum into a bar-based comping engine — set Direction to
  Up-Down for authentic alternating strokes across the repeats.

Each note-off is delayed by the same amount as its note-on, so notes keep their
length, the fan stays in order, and nothing ever hangs. Strum works while the
transport is stopped too (live chords strum as you play them); only Repeat needs
the transport, since it re-strikes on the bar grid. As a buffered time module it
follows the shared transport rules: on stop, material still in flight is
discarded and anything sounding is released. The node shows its spread gap
(Off / 1/16 / 1/8, or ms between the detents) as a sublabel.

User settings:

- Spread — Off to an 1/8-note gap per note, 1/16 at the dial's middle
  (default 40% of an 1/8); Off = bypass, in-between values read in ms at the
  current tempo.
- Direction — Up (default), Down, Up-Down, or Random.
- Curve — Even (default), Accelerate, or Decelerate.
- Velocity — −100% to +100% (default 0%); loudness tilt across the fan.
- Jitter — 0% to 100% (default 0%); random per-note timing/velocity looseness.
- Repeat — Endless (default), 1/4 bar, 1/2 bar, 1 bar, 2, 4, 8, or 16 bars.

### Humanize (modulator)

Humanize is the "performance feel" pass: it loosens machine-tight material
into something a player might have played. Its natural home is just before an
Output, where it shapes everything flowing through — played notes, generated
notes, quantized notes, and delay echoes alike — along two axes laid out to
match the settings window: a top row of *structured groove* you dial in
deliberately, and a bottom row of *random human touch*.

Groove (top row, all locked to the Rate grid):

- **Swing** pushes every off-beat late, the same pair-based model the Quantize
  modulator uses (`swing-timing.md`) — but applied as a nudge, not a snap, so it
  shuffles the timing without quantizing notes onto the grid. On-beats stay put;
  ~60–70% is the classic triplet shuffle.
- **Lay-back** drags every note a constant amount behind the beat — the
  "relaxed drummer" feel — up to half a step at 100%.
- **Accent** emphasises velocity by metric position: notes on the strong beat of
  each pair get louder, off-beat notes softer (±40% at full), so a flat
  generated line breathes with the pulse.

Human touch (bottom row, random but repeatable):

- **Timing** jitter nudges each note-on a random amount late.
- **Velocity** jitter varies each note's velocity up and down.
- **Length** jitter lengthens each note by a random amount.

All timing moves are delays — a live MIDI effect can only hold a note back,
never play it earlier than it arrived — so lay-back and jitter drag and lengthen
rather than rush. The random amounts are drawn from the song position, not a
free-running dice roll, so the humanised feel is *repeatable*: a looped section
plays with the same "human" timing every pass instead of shimmering. Like
Quantize, Humanize only acts while the transport plays (stopped, notes pass
straight through for immediate live feel), and it follows the shared
no-hanging-notes rule across a transport stop.

Because swing and accent need to know where the beats are, Humanize carries a
Rate (the groove grid); it maps no pitch, so it has no Root/Scale. Every amount
is a 0–100% dial (default 0%, i.e. a fresh Humanize passes everything through
untouched until dialled in).

User settings:

- Rate — 1/32 to 1/1 (default 1/16); the grid swing and accent lock to.
- Swing — 0% to 100% (default 0%); off-beat lateness (pair-based).
- Lay-back — 0% to 100% (default 0%); constant drag behind the beat.
- Accent — 0% to 100% (default 0%); strong/weak velocity emphasis.
- Timing — 0% to 100% (default 0%); random note-on lateness.
- Velocity — 0% to 100% (default 0%); random velocity variation.
- Length — 0% to 100% (default 0%); random note lengthening.

Not yet exposed: a true *push* (playing ahead of the beat), which a zero-latency
MIDI effect can't do without reporting host latency — lay-back covers the
musically dominant, drag-behind half.

## Planned — speced

These are concrete enough to build. Each entry states the intended behaviour;
open details, where any, are flagged.

### Generators

- **Step Sequencer** — the user draws a fixed melodic pattern in a mini piano
  roll and the module plays it in a loop. The grid UI should follow the shared
  `design/grid-interaction.md` conventions.
(The Harmonizer — single notes into chords — is now implemented; see its entry
under "Implemented modules" above.)

### Modulators — time and rhythm

- **Ratchet / Repeat** — subdivides or retriggers a note into a burst
  (settings: subdivision count and, likely, a velocity ramp).
- **Note Length / Legato** — overrides or scales gate length, from staccato
  through fully legato.

(A separate Swing / Groove module was planned here; the Quantize modulator's
swing setting covers it.)

### Modulators — rhythm application

- **Euclidean** — distributes N hits across M steps (the classic Euclidean
  rhythm) and imposes that timing on incoming pitch material: the input
  supplies pitch only, Euclidean supplies timing. On each hit it retriggers
  whichever pitches are currently held or active from the incoming stream —
  not a history buffer, not round-robin. If a chord is held, the whole held
  set retriggers together. This is the model for all rhythm-applying
  modulators: they override the input's own timing, unlike gating modules,
  which mask notes but keep their timing. (The implemented **Rhythmize** is
  exactly this retrigger-the-held-set mechanic driven by a hand-set 16-step
  pattern; a Euclidean module would generate the pattern from N-hits-in-M-steps
  instead.)

### Modulators — dynamics

- **Velocity Shaper** — modulates velocity with an LFO or a drawn curve
  (settings: shape, rate/length, depth).

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
  modulators it keeps the surviving notes' own timing — which is what keeps it
  distinct from the implemented Rhythmize (same 16-step UI, but Rhythmize
  re-emits held notes on its own grid; Rhythm would mask passing notes without
  re-timing them). Its step-grid UI can reuse RhythmizeStepGrid as-is.
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
