# Module window

The settings surface every module opens on a double-click. This documents the
design decisions behind it — the shape, the rules for what goes where, and the
tradeoffs we deliberately took — so the rollout to the remaining modules stays
consistent. It is plugin-local: the cross-product UI rules (themes, typography,
panels, modal dialogs) live in `SnorkelAudioStandards/design/`; this file only
covers Current's module window and where it leans on those shared rules.

Status: all fourteen modules are on this window — all five generators
(Random, Scale gen, LFO, Chord, Drone) plus Arp, Quantize, Scale mod, Shift,
Delay, Humanize, MIDI In, Output, and **Progression**. Progression's variable-length step
list has no home in the fixed six-cell grid, so it rides the **custom-body
escape hatch** (see "Custom body" below) rather than the grid.
Implementation lives in `ModuleWindow.h/.cpp`; the dial rendering is in
`CurrentLookAndFeel`. Shared controls go through `ModuleWindow` helper pairs in
`Canvas` (`addRootScaleMenu` / `addRateMenu` / `addHoldLengthMenu` /
`addGateDial` / `addOctavesDial` / `addModeCombo` / `addRepeatCombo` /
`addHoldRepeatCombo`), so a shared setting is the identical control across
modules.

## Goal

Every module's settings should read as the same window — one recognisable
frame, controls always in the same place — rather than each module inventing
its own dialog. A user who has learned one module's window already knows where
Root, Scale, and the module's own controls sit in the next.

## Anatomy

Top to bottom, the window is always these four bands:

- **Title** — the module name (e.g. "Random").
- **Menu bar** — a thin strip echoing the global (editor) menu bar, three
  fixed slots: Root, Scale, and a Rate *or* Length combo.
- **Grid** — a 3x2 block of six cells for the module's own controls. Each cell
  is a combo or a dial with its label above it.
- **Buttons** — OK / Cancel, bottom-right.

It is a modal overlay inside the editor (not an OS window): it dims and covers
the whole editor, cancels on Esc or a click outside the panel, and confirms on
Return. This is the same overlay contract as `InlineDialog`, and it satisfies
the SnorkelAudioStandards modal-dialog rule (no `juce::AlertWindow` /
`DialogWindow`).

## The menu bar

- Holds the settings that recur across most modules and belong "above" the
  module's own controls: **Root**, **Scale**, and the module's primary time
  base — **Rate** for the note-length modules, **Length** for the bar-based
  ones (Chord, Drone; and Progression's bar-length rate).
- **Same dimensions as the global menu bar.** Root and Scale reuse the editor
  menu bar's exact label and combo widths and the same control height, so the
  two bars read as the same family. Labels sit to the *left* of their combo
  (horizontal), matching the global bar.
- **Fixed three-slot layout with reserved blanks.** A module that lacks one of
  these settings leaves that slot empty rather than closing the gap — the row
  still reads as the shared menu bar, and the same control never jumps
  position between modules. (Random fills all three; a module with no
  rate/length would leave the third slot blank.)

## The grid

- **3x2, six cells, label above the control.** The label-above layout and the
  cell sizing are taken from Little Arp Monster's Input/Arp section boxes — the
  reference the design brief pointed at. Cells fill row-major (top row first).
- **Combo or dial per cell.** Use a **dial** where the value reads well as a
  knob — octaves, gate, and other small bounded ranges — and a **combo** for
  pick-from-a-list values. Random's two controls are the note-range endpoints,
  shown as dials over the 0–127 MIDI span.
- **Unused cells stay blank.** A module with fewer than six own-controls leaves
  the remaining cells empty; the grid box still frames the same area. (Random
  uses two cells; four are blank.)
- **Uniform cell height, even for combo-only rows.** Every cell reserves the
  full dial height, so a combo sits vertically centred in the same tall cell a
  dial would occupy. This keeps rows aligned control-for-control across
  modules whether a given cell is a combo or a dial — the cost is a combo-only
  module looking a little airy, which we accept for the cross-module
  alignment.

## Custom body (the escape hatch)

- **When the six cells can't hold a module's controls**, `setCustomBody` swaps
  the grid for a caller-supplied component and sizes the panel to it. The rest of
  the window is unchanged — same title, menu bar, recessed section frame, and
  OK/Cancel row — so the module still reads as the shared window. This is a
  first-class (if narrow) part of `ModuleWindow`'s surface, not a one-off hack;
  any future module with an odd body reuses it.
- **Progression is the one user today.** Its settings are a **variable-length
  step list** (1–8 steps), which a fixed six-cell grid can't grow. The body is
  `ProgressionStepList`: a left-to-right row of step cells (the scale degree
  drawn big, the octave offset as a corner tag), a trailing **append cell** whose
  two halves are action arrows — right/top adds a step, left/bottom removes the
  last — and, below the row, **Degree** and **Octave** combos that edit the
  selected cell. The interaction *feel* (row of cells, append/remove at the end,
  select-then-edit) is borrowed from Little Sequencer's arranger tab; the scenes
  the arranger carries are dropped (Progression has no scenes). The menu bar
  still carries Root / Scale / Length above it through the usual helpers.
- **Rejected alternative:** capping steps to fit six static cells. That would be
  a feature regression (the point of the step list is that it grows), so the
  custom body won over shrinking the data to the grid.

## Dials

- **Flat-dot rotary**, ported from LAM (`CurrentLookAndFeel::drawRotarySlider`):
  a filled body, a thin outline, and an accent dot marking the value — no arc,
  no gradient. Reads cleanly on both themes; recolours for free on a theme
  swap because it paints from `CurrentTheme::active()`.
- **No text box on the dial itself.** A dial's value instead appears as a
  **live readout folded into the cell label**: the label reads "From: C2" and
  tracks the knob as it turns. We chose the in-label readout (over a separate
  value line below the dial, or a value drawn inside the knob) because it costs
  no extra vertical space and keeps the name and its value together on one
  line. `setGridDial` takes an optional value formatter, so each dial supplies
  its own text — note names for a note range, "2" for octaves, "50%" for gate.
  A dial without a formatter just shows its plain label.
- **A readout can depend on another control.** Shift and Delay's shift amount is
  a dial whose unit word is read off the Scale combo — `+3 steps` with a scale,
  `+3 semitones` with Scale = Off. The formatter captures the window and queries
  the Scale combo, and `setComboChangeCallback ("scale", …)` re-runs
  `refreshDial` when Scale flips, so the unit tracks live and not just on a dial
  turn. This is the general hook for one control reacting to another; keep such
  cross-cell coupling to genuinely linked settings so the window stays legible.

## Decisions we took (and the roads not taken)

- **Modal popup, not an embedded panel.** The window keeps the existing
  double-click-to-edit, OK/Cancel interaction; we restructured the *inside* of
  the settings surface, not how it is summoned or dismissed.
- **Combos keep their down-arrow.** The SnorkelAudioStandards no-arrow combo
  rule is not yet adopted anywhere in Current (the global menu bar combos show
  arrows too), so the module window matches the rest of the plugin rather than
  diverging on its own. Adopting no-arrow combos is a separate, plugin-wide
  change.
- **Shared-control helpers.** The `ModuleWindow` has its own `add/read` helper
  pairs in `Canvas`, the twins of the `InlineDialog` ones, so a shared setting is
  the *identical* control on either window. Every module routes its
  Root/Scale/Rate/Length/Gate/Octaves/Mode/Repeat through them — Arp, for
  instance, reuses the generator helpers verbatim and needed no new code.
  Progression uses the menu-bar helpers for Root/Scale/Length; its step rows are
  bespoke to `ProgressionStepList` (a growable list has no shared-helper form).

## Layout constants

The exact pixel sizes live as constants in `ModuleWindow.h` and are
plugin-local by the panels-controls standard (only the *recipe* is shared, not
the numbers). The intent behind them: Root/Scale at the global menu bar's
dimensions, ~70 px square dials and ~26 px combos (the LAM range), a panel
sized to the menu bar's three-group width. Keep them in one place so the whole
window scales together if the sizing is ever revisited.
