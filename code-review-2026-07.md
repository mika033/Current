# Current — Architecture & Code Review (July 2026)

A point-in-time review of the codebase at the end of Phase 2, before Phase 3 is
scoped. Full read of every header and source file (~5,400 lines), cross-checked
against `architecture.md`, `modules.md`, and
`generative-midi-plugin-requirements.md`. Verified by a clean Linux build with
tests enabled and a green `current_engine_test` run. Findings are ranked by how
much they matter for Phase 3, not by how easy they are to fix.

## Overall assessment

The codebase is in good shape for its stage. The things that are hardest to
retrofit are already right:

- The no-hanging-notes discipline is real, not aspirational. The
  `activePass` (incoming → emitted) bookkeeping, the release-from-bookkeeping
  rule for note-offs, and the transport-stop flushes are consistently applied
  across pass-through, generators, Quantize, and Delay — and the smoke test
  asserts on/off balance for every module, including the nasty combinations
  (gate 100%, mid-note channel edits, stop with buffered material).
- The layering is clean: `Engine` has no GUI dependencies and is fully
  testable headless; the canvas model lives in the processor and survives
  editor close; the docs describe the code accurately in almost every detail
  (the exceptions are listed under Documentation drift below).
- The shared-settings idea (one option table, one dialog-control pair, one
  engine mapping per recurring setting) is carried through consistently and is
  the right foundation for a large module catalogue.
- The smoke test is genuinely good — behaviour-level, deterministic where it
  can be, and it covers the settings, not just the defaults.

The findings below are mostly about what Phase 3 will collide with, plus a
handful of engine corner cases and doc drift.

## 1. The engine never reads the host's song position (FIXED July 2026)

**The single most important finding.** `Engine::process` reads only
`isPlaying` and BPM from the playhead; it never looks at `ppqPosition`. All
grids — step clocks, repeat windows, the LFO cycle, the Progression playhead,
Quantize's grid and swing parity — are counted in samples from the moment the
transport last started, with "the first step fires at sample 0".

The docs say "counted from transport start" throughout, so code and docs
agree; but the musical consequences are probably not what a DAW user expects:

- Press play anywhere but exactly on a bar line and every generated note is
  offset from the host's bars and beats by that amount, for the whole
  playback. Pressing play on beat 2.5 puts the Scale generator's "one-bar run"
  across bar lines and Quantize's swung grid off the beat.
- Host loop wrap: the counters freewheel through the wrap, so repeat windows,
  the Progression's step position, and the LFO cycle drift from the song
  whenever the loop length isn't an exact multiple of that module's window.
  Looping four bars against an 8-bar progression means the progression is at a
  different place on every pass — which may even be desirable — but a 3-bar
  loop against a 1-bar repeat window puts the *pattern itself* out of phase
  with the bar.
- Tempo automation accumulates drift, same root cause.

The fix direction is to re-anchor every clock from `ppqPosition` each block
(deriving "steps since bar 0" from the host position instead of counting
locally), which is presumably what LAM already does per the requirements' "same
way LAM solves this" note for rate sync. This is a product decision as much as
a code change — "from transport start" vs "from the song's bar 0" changes how
every module feels — and it gets more expensive the more modules exist, so it
should be decided at the start of Phase 3, before the catalogue grows.
Recommendation: anchor to host position. (Decided: this will be fixed soon —
see "Fix preparation for findings 1 and 2" below.)

## 2. The Standalone build is musically inert (FIXED July 2026)

The requirements call out that rate is host-synced "except in Standalone mode
(no host to sync to) — handled the same way Little Arp Monster (LAM) solves
this". Nothing implements that: in the Standalone there is no playhead, so
`isPlaying` is always false and every stepped module is silent forever. Only
pass-through and Delay echoes work. Neither `architecture.md`'s deferred list
nor `modules.md` mentions this gap, so a future session (or a tester using the
Standalone, which the build script launches by default) would rediscover it
the hard way. Needs the LAM-style internal transport. (Decided: this will be
fixed soon — see "Fix preparation for findings 1 and 2" below.)

## Fix preparation for findings 1 and 2 (for the next session)

Both fixes are planned to land soon. Both problems are already solved in
Little Arp Monster — the next session should pull the LAM repo into its
workspace (via add_repo; ask the user for the exact repo name if needed) and
copy the approach rather than re-derive it. This review deliberately did not
open the LAM repo, so the notes below map out the Current side only: where the
change lands, what has to be decided, and what to look up in LAM.

### Finding 1 — anchor the engine's clocks to the host position

What to look up in LAM: how it derives its step grid from the playhead each
block (it implements the standards repo's `swing-timing.md`, so it almost
certainly computes grid positions from `ppqPosition` rather than counting
locally), and how it behaves when a host supplies a playhead without a ppq
value. The standards repo's `swing-timing.md` itself defines the pair-based
swing maths in loop-length-invariant (i.e. position-anchored) terms — good
cross-reference for the Quantize part.

The Current side, concretely:

- Everything to change is inside `Engine::process` (`source/Engine.cpp`);
  `Engine::Config` and the processor handoff are untouched. The current
  anchoring is the `isPlaying && ! wasPlaying` block (which zeroes
  `arpSamplesToNext` / `randomSamplesToNext` / `scaleSamplesToNext` /
  `lfoSamplesToNext`, `arpIndex`/`arpStep`/`scaleStep`/`lfoStep`,
  `quantSamplesToNext`/`quantStep`, `progQn`) plus the freewheeling advance at
  the bottom of `process` (`quantSamplesToNext`, `quantStep`, `progQn`).
- The fix direction: read `pos->getPpqPosition()` each block and *derive*
  positions instead of carrying counters. Per stepped module: the grid index
  at block start is floor(ppq / stepQn); a step fires at every boundary whose
  ppq lies in [blockStartPpq, blockEndPpq), converted to a block sample via
  samplesPerQn. Repeat-window position becomes fmod(ppq, repeatQn); the LFO's
  cycle position becomes fmod(ppq / cycleQn + phase, 1); the Progression index
  becomes floor(ppq / progRateQn) % stepCount (replacing `progQn`); Quantize's
  straight boundary index (whose parity drives swing) is floor over
  quantStepQn the same way. Tempo changes and loop wraps then come out right
  with no special cases.
- Decisions to make deliberately, not by accident:
  - Boundary ownership must be half-open ([start, end) per block) or a loop
    wrap landing exactly on a step will double-fire or skip it.
  - Negative ppq exists (host pre-roll / count-in): floor and fmod must be the
    mathematical versions (round toward −∞), not C truncation, or the grid
    misaligns before bar 1.
  - The Arp's walk position (`arpIndex`) currently advances only when a note
    fires. Deriving it from the grid index instead makes the arp phrase
    identical on every loop pass — almost certainly the musical intent, but it
    is a behaviour change; check what LAM does.
  - Keep the 4/4 assumption for the bar-based tables (repeat, bar lengths) —
    reading the host time signature is a separate, later step, and
    `ModuleSettings.h` already documents the assumption.
- The smoke test builds its `PositionInfo` with bpm + isPlaying only; it needs
  `setPpqPosition` fed per block (a small advancing-position helper), plus the
  two tests already suggested in the coverage section: play starting mid-bar
  (first step must land on the next grid point, not at sample 0) and a
  simulated loop wrap (ppq jumping back; pattern position must follow).

### Finding 2 — give the Standalone (and playhead-less hosts) a transport

What to look up in LAM: where it detects "no usable host transport" and what
it substitutes — whether it free-runs at a fixed/user-set BPM, whether the
Standalone gets a tempo control or play/stop affordance in the UI, and where
that state lives (parameter? standalone-only widget?). Copy those product
choices; they are exactly the "handled the same way LAM solves this" the
requirements call for.

The Current side, concretely:

- Do finding 1 first. Once the engine consumes a ppq position, the fallback
  is just a synthesized position, and the engine keeps a single code path.
- Implement the fallback in `CurrentAudioProcessor::processBlock`, not in the
  engine: where `pos` is built from `getPlayHead()` today, detect the missing
  cases (null playhead — the Standalone; a playhead without bpm or ppq) and
  substitute a processor-owned internal transport: isPlaying true, bpm from
  whatever source LAM uses, ppq accumulated across blocks
  (+= numSamples / samplesPerQn). The engine then never knows the difference.
- The accumulator is audio-thread-owned state (like the engine's counters);
  reset policy (does the Standalone's transport ever rewind?) should follow
  LAM.
- The smoke test can then cover the fallback by calling the engine with a
  null position — today that path silently produces no generator output,
  which is exactly finding 2.

## 3. The settings handoff should become a snapshot swap before wiring lands

`architecture.md` already names the per-field atomics as the design's biggest
seam, to be replaced by an atomically swapped immutable graph snapshot when
wiring lands. The review's addition: do the swap *first*, as the opening move
of Phase 3, rather than alongside wiring — because the current scheme is
already at its limits and every new module makes the eventual change bigger.

- The processor now carries ~45 named atomics plus a packed-step array, ~90
  lines of `refreshEngineConfig` stores and ~65 lines of `processBlock` loads,
  all transcribing `ModuleSettings` field by field into `Engine::Config`.
  Every new module extends all three by hand.
- The "a half-updated combination is indistinguishable from the edit landing
  one block later" argument (stated in `PluginProcessor.h` and
  `architecture.md`) is slightly overstated: a multi-field edit (say Shift's
  amount and scale changed together) can be seen by one block as a combination
  that never existed as a user state — new amount with old scale. Same for two
  Progression steps replaced in place. At one block's duration this is
  inaudible in practice, which is why it hasn't hurt; but the rationale as
  written would also "justify" much worse tearing later.

A message-thread-built `std::shared_ptr<const EngineSnapshot>` (presence +
resolved per-module settings), published via atomic exchange and loaded once
per block, eliminates all three transcription sites, makes every edit
all-or-nothing, and *is* the graph-snapshot mechanism wiring needs — wiring
then only changes what the snapshot contains. One RT detail: retire old
snapshots on the message thread (a small retention list) so the audio thread
never frees memory. `Engine::Config` and `ModuleSettings` could collapse into
one struct at the same time, removing the index-vs-quarter-notes duality.

## 4. Per-module knowledge is scattered across seven places

Adding module #12 today means touching: the enum + catalogue + two string maps
(`ModuleTypes.h`), settings fields and option tables (`ModuleSettings.h`),
`Engine::Config` + engine logic + engine state fields, the processor atomics +
`refreshEngineConfig` + `processBlock` (finding 3 removes these), the two
per-type property lists in `getStateInformation` / `setStateInformation`, and
in `Canvas` a dispatch branch, an `open*Dialog` method, and a sublabel branch
(the sublabel logic exists twice: in `addNodeComponent` and again inside each
dialog's OK handler). With ~20 modules planned, this per-module cost is the
main scaling problem in the codebase. Three cheap consolidations, all doable
without inventing a framework:

- **Persistence: save every settings field for every non-I/O module.** The
  per-type property lists exist to keep the saved XML minimal, but they are
  the fourth and fifth encodings of "which type uses which fields", and the
  pre-release policy explicitly permits format changes. Writing the whole blob
  unconditionally removes ~100 lines including the fiddly restated drop-time
  defaults in the load path (the ScaleGen/Delay rate special cases).
- **One `sublabelFor (type, settings)` helper** replacing the type-chain in
  `addNodeComponent` and the per-dialog copies, so a node's label logic exists
  once.
- **A per-type dialog registry.** Each `open*Dialog` is the same scaffold
  (fetch settings, build controls, OK/Cancel, re-fetch, read controls, store,
  refresh sublabel) around a per-type build/read pair. Registering
  build/read functions per type (in the catalogue or a parallel table) turns
  the `onOpenSettings` if-chain and ten near-identical methods into one
  generic open function plus small per-type parts. Worth doing before the
  next batch of modules, not after.

## 5. Engine corner cases

None of these are Phase 2 blockers; listed in decreasing order of likelihood
that a user ever hits them.

- **Arp vs. notes held across a transport start.** Hold a key while stopped
  (it passes through and sounds), then press play: the sustained note keeps
  ringing (its note-off only comes when the key is released) while the Arp
  starts arpeggiating the same pitch on top — contradicting "the held notes
  are consumed and do not sound directly". Fix: on the stop→play transition
  with an Arp present, release `activePass` entries for currently held keys.
  The mirror case (a key swallowed while playing keeps not sounding after
  stop until re-struck) is defensible as-is but worth a doc sentence.
- **Quantize swing + long gates can cut a repeated pitch.** Gates are capped
  at one sample short of the *straight* step, but swing shortens the odd→even
  gap by swing/2 of a step. A note deferred to a late (odd) point with gate
  fraction > 1 − swing/2 (e.g. Arp gate 100%, any swing; gate 75%, swing >
  50%) overlaps the next note; if that next note is the same pitch (single
  held note under an Arp), its note-on is immediately killed by the earlier
  note's off. Fix: when quantizing, cap the deferred note's gate at the
  distance to the next swung grid point.
- **`prepare()` discards sounding notes without releasing them.** A host that
  calls `prepareToPlay` mid-playback (sample-rate or buffer-size change)
  clears `activeGen`/`activePass` without note-offs — hung notes on the
  synth. Unavoidable to fix perfectly (there is no buffer to write offs into
  at prepare time), but the lists could be kept across `prepare` instead of
  cleared, so the offs still go out on the next block.
- **`held[]` is channel-blind.** The same pitch held on two channels (two
  MIDI Ins) is one entry; a note-off on either channel silences the Arp's
  view of both. Fine for one keyboard; will matter with real multi-input
  wiring.
- **A retriggered pitch waiting in Quantize doubles up.** Two note-ons for
  the same key before its grid point land as two identical deferred notes
  firing at the same sample. Harmless on most synths; a `pendingQuant`
  duplicate check would tidy it.

## 6. Shift overrides the scale but not the root (product question)

Every other module with a scale setting pairs it with a root (the shared
root+scale control). Shift's dialog offers only the scale list, and the engine
walks degrees using the *global* root even when a named scale is chosen — so
"Shift in D major" with the global root on C actually shifts in C-rooted
major-scale intervals. If that's intended (the scale choice meaning "interval
pattern only"), `modules.md` should say so explicitly; if not, Shift should
grow the same root override as its siblings. Either way the current behaviour
is surprising relative to the shared-settings promise of "the identical
control everywhere".

## 7. Documentation drift

- `architecture.md`'s "Deferred, by design" list still contains "per-module
  settings (the InlineDialog placeholder is the hook)" and "I/O modules on
  the canvas" — both shipped long ago, and the same document's opening says
  so. The list needs re-basing to actual Phase 3 candidates.
- Chain order for Delay vs. Output: `modules.md` says "…Quantize re-times…,
  the Delay adds its echoes, and everything exits through Output", but in the
  code (and in `Engine.h`'s and `architecture.md`'s descriptions) echoes are
  booked *after* Output stamping — echoes derive from the final emitted
  stream, one echo chain per emitted copy. `modules.md`'s sentence should
  move Delay after Output.
- `generative-midi-plugin-requirements.md` still lists Arp under Generators,
  still names a global quantize setting in the menu bar, and still describes
  "Quantize / Scale-Snap" as one pitch-snapping modulator — all three
  superseded by decisions recorded in `modules.md`/`CLAUDE.md`. Since the
  phases section *was* updated (renumbering), the document reads as
  maintained, so these stale entries can mislead. Either update them or mark
  the module lists as superseded by `modules.md`.
- Neither doc mentions the Standalone limitation (finding 2).

## 8. Smaller code notes

- `PluginProcessor::setStateInformation`'s closing comment contemplates hosts
  calling it off the message thread, but the threading model (and
  `architecture.md`) requires state load/save on the message thread —
  `moduleList` is written here unguarded. The comment should not imply
  off-thread calls are safe; if a host is ever found doing it, marshal to the
  message thread instead.
- `InlineDialog::resized()` grabs keyboard focus into the first text field on
  every resize, so a window resize mid-edit steals focus/caret. Guard it to
  first-show.
- `CurrentTheme::setActive` stores a pointer to the passed reference — safe
  for the two static schemes it's used with, but a footgun signature; taking
  the index (like `byIndex`) would remove the dangling risk.
- `MenuBar::paint` re-applies the title label colour every paint; the
  LookAndFeel already sets `Label::textColourId`, so this line can go.
- The `showInlineDialog` raw-new / caller-deletes contract works (JUCE's
  click callbacks tolerate deletion) but every caller repeats the
  remove-and-delete epilogue; a `dismiss()` on the dialog would remove eight
  copies of it. Fold into the finding-4 dialog registry if that happens.

## 9. Test coverage

`current_engine_test` covers the implemented behaviours well. Worth adding as
Phase 3 begins, roughly in this order:

- Host-position anchoring tests (start mid-bar, loop wrap) — they define the
  finding-1 behaviour before it's built.
- A regression test for the swing + full-gate overlap (finding 5) once fixed.
- Arp fed through a MIDI In filter (filtered notes must not reach `held`).
- A state round-trip test at the processor level (save → load → compare
  module list); needs a small harness beyond the engine-only test, and would
  have caught any per-type property-list slip — or becomes trivial once
  finding 4's save-everything change lands.

The pluginval strictness-10 pass predates this session; re-run after the
finding-3 refactor since it stresses state and editor lifecycles.

## Suggested Phase 3 opening order

1. Decide finding 1 (host-position anchoring) and finding 6 (Shift root) —
   product calls that shape everything after.
2. Land finding 3 (snapshot handoff) while the module count is still eleven.
3. Land finding 4's consolidations (persistence, sublabels, dialog registry).
4. Fix the finding-5 corners with tests.
5. Re-base the docs (finding 7) in the same pass.

Then wiring and the new modules build on a base that no longer grows in cost
per module.
