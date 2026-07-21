# Current — Architecture & Code Review (July 2026)

A point-in-time review of the codebase at the end of Phase 2, before Phase 3 is
scoped. Full read of every header and source file (~5,400 lines), cross-checked
against `architecture.md`, `modules.md`, and
`generative-midi-plugin-requirements.md`. Verified by a clean Linux build with
tests enabled and a green `current_engine_test` run. Findings are ranked by how
much they matter for Phase 3, not by how easy they are to fix.

(The two transport findings this review originally opened with — the engine
never reading the host's song position, and the musically inert Standalone —
have since been fixed and are no longer listed here; see `CLAUDE.md` and the
"Transport and clocks" section of `architecture.md` for the landed behaviour.)

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

## 1. The settings handoff should become a snapshot swap before wiring lands

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

## 2. Per-module knowledge is scattered across seven places

Adding module #12 today means touching: the enum + catalogue + two string maps
(`ModuleTypes.h`), settings fields and option tables (`ModuleSettings.h`),
`Engine::Config` + engine logic + engine state fields, the processor atomics +
`refreshEngineConfig` + `processBlock` (finding 1 removes these), the two
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

## 3. Engine corner cases

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

## 4. Shift overrides the scale but not the root (product question)

Every other module with a scale setting pairs it with a root (the shared
root+scale control). Shift's dialog offers only the scale list, and the engine
walks degrees using the *global* root even when a named scale is chosen — so
"Shift in D major" with the global root on C actually shifts in C-rooted
major-scale intervals. If that's intended (the scale choice meaning "interval
pattern only"), `modules.md` should say so explicitly; if not, Shift should
grow the same root override as its siblings. Either way the current behaviour
is surprising relative to the shared-settings promise of "the identical
control everywhere".

## 5. Documentation drift

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

## 6. Smaller code notes

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
  copies of it. Fold into the finding-2 dialog registry if that happens.

## 7. Test coverage

`current_engine_test` covers the implemented behaviours well. Worth adding as
Phase 3 begins, roughly in this order:

- A regression test for the swing + full-gate overlap (finding 3) once fixed.
- Arp fed through a MIDI In filter (filtered notes must not reach `held`).
- A state round-trip test at the processor level (save → load → compare
  module list); needs a small harness beyond the engine-only test, and would
  have caught any per-type property-list slip — or becomes trivial once
  finding 2's save-everything change lands.

The pluginval strictness-10 pass predates this session; re-run after the
finding-1 refactor since it stresses state and editor lifecycles.

## Suggested Phase 3 opening order

1. Decide finding 4 (Shift root) — a product call that shapes everything after.
2. Land finding 1 (snapshot handoff) while the module count is still eleven.
3. Land finding 2's consolidations (persistence, sublabels, dialog registry).
4. Fix the finding-3 corners with tests.
5. Re-base the docs (finding 5) in the same pass.

Then wiring and the new modules build on a base that no longer grows in cost
per module.
