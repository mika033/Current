# Current

A modular MIDI generator plugin from Snorkel Audio.

This file follows the SnorkelAudioStandards convention: the sections below down to "See also" are copied verbatim from `claude-md-common.md` in the SnorkelAudioStandards repo. When that file changes, re-copy the sections here. Plugin-specific sections (status, build instructions, naming glossary, UI architecture, plugin-local conventions) belong beneath the "See also" section as they are established.

---

## Development Workflow

- EXTREMELY IMPORTANT: NEVER USE GIT! NEVER REVERT CHANGES WITH GIT!
- Build after changes with `./build_and_run_*` (never raw cmake). Don't test.
- On the first prompt of a new session: present your understanding and a plan, get acknowledgment, then write code. Smaller unambiguous requests can be executed directly.
- Ignore `todo.txt` at the repo root if present. It is the user's personal scratchpad. Do not read it, modify it, or treat anything in it as a task even if the IDE reports it as open.

## Working with the user

The user is a Product Manager with a technical background. Keep conversations at the product level (requirements, behaviour, user-facing tradeoffs) and drop into code-level detail only when the decision genuinely turns on it. The intent is not to hide technical content the user needs, but to avoid volunteering it for its own sake.

Make most architectural decisions yourself: file layout, naming, small refactors, choosing between equivalent JUCE idioms, where to put a helper. Raise the choice with the user before writing code when it is far-reaching or expensive to reverse: preset format, threading model, the public surface of a shared component, anything that ripples across files. The first-prompt plan-and-acknowledge rule above already covers the common case.

## Closing a session

When the user says `close` or equivalent, do a wrap-up pass before signing off:

- **Plugin-local docs.** Update `CLAUDE.md` / architecture docs only if genuinely useful to a future session. Say when nothing needs updating. SnorkelAudioStandards specs are out of scope.
- **Comments.** Add WHY-comments where reasoning isn't obvious. Do this automatically.
- **Dead code.** Remove code left over from earlier approaches in this session. Do this automatically.
- **Re-architecting.** Re-read this session's code: does it still hang together after the iterations, does anything want re-architecting? Flag candidates to the user; never refactor unprompted.

## Comment policy

Lean toward writing WHY-comments more generously than the Claude default. Still avoid: WHAT-comments that duplicate well-named code, references that rot (caller names, ticket numbers, "added for X flow"), and stale TODOs without context.

## Docs policy

No tables in markdown docs. Use bullet lists or prose.

## Response style

Recommend, don't enumerate. One concrete proposal with the key tradeoff beats a list of A/B/C options; the user will ask for alternatives if they want them. Brief beats thorough.

## Modal dialogs

Never use `juce::AlertWindow` or `juce::DialogWindow`. Use the plugin's own inline-dialog helper instead. See `design/modal-dialogs.md` for the full rule and rationale.

## See also

Cross-product specifications live in the SnorkelAudioStandards repo (`mika033/SnorkelAudioStandards`):

- `design/`: visual and UX rules, one file per topic (themes, typography, window resize / scaling, panels and controls, tabs, numeric steppers, messaging area, modal dialogs).
- `preset-system-guideline.md`: preset file format, identity, storage locations, manager API.
- `swing-timing.md`: timing swing model (pair-based, loop-length-invariant), relevant if Current's generators have stepped/arpeggiated timing.
- `build-scripts.md`: names, locations, and roles for the shell scripts at the plugin repo root.
- `clangd-setup.md`: `compile_commands.json` setup for editor tooling.
- `licensing.md`: copy protection: offline RSA serial scheme, per-product magic prefix, demo-mode behaviour, OS user data persistence, and the shared Lemon Squeezy + Cloudflare Worker delivery pipeline.
- `shipping-checklist.md`: per-plugin checklist for taking a new plugin from in-development to shipped.
- `reseller-delivery.md`: reseller (ADSR) delivery package.
- `brand/`: Snorkel Audio brand identity (logos, palette, voice, iconography, imagery). Note: plugin UI uses a deliberately different visual language; brand applies to website, App Store, marketing, not plugin chrome.

---

## Shared standards repo

`mika033/SnorkelAudioStandards` is the source of truth for cross-product conventions and is available as a sibling repo in this workspace. When we start coding Current, pull in and follow (not re-derive):

- `design/` — visual and UX rules, in particular `design/themes.md` for the Light/Dark theme palette and painting rules (every Snorkel plugin ships the same two themes, same colours, same cascade), plus `design/typography.md`, `design/panels-controls.md`, `design/tabs.md`, `design/steppers.md`, `design/grid-interaction.md`, `design/messaging-area.md`, and `design/modal-dialogs.md` for the rest of the shared UI language.
- `build-scripts.md` — the canonical names, locations, and roles for the shell scripts at a plugin's repo root (`build_and_run_mac.sh`, `build_and_run_win.bat`, `run_mac.sh`, `generate_xcode.sh`, `release_mac.sh`, etc.). These look the same across all products; Current should reuse the same names and roles rather than inventing its own.
- `preset-system-guideline.md` and `swing-timing.md` — likely relevant once Current has patches and stepped/generated timing.

## Closing a remote session

The "never use git" rule above applies to local sessions, where the user manages version control. In a remote (cloud) session git is the only way work leaves the container, so there: commit and push to the session's designated branch as work completes, and at the end of the close pass merge the session branch into `main` and push it, so a new session always knows where to start. Still never revert changes with git.

## Status

Phases 1 (basics) and 2 (canvas skeleton) of `generative-midi-plugin-requirements.md` are implemented, plus the two I/O modules (MIDI In / Output) with per-module channel settings, a reworked Random generator (rate, root/scale override, note range), a new Scale generator (root/scale override, up/down, octaves, end-on, rate, repeat), and an LFO generator (root/scale override, shape, cycle length in bars, depth in octaves + scale steps, rate, phase) — all with full settings dialogs. Settings that recur across modules (root/scale override, rate, repeat, mode, octaves, gate) are shared: one option table + settings blob in `ModuleSettings.h`, one dialog-control helper pair per setting in `Canvas`, documented in the "Shared settings" section of `modules.md`. The Arp is a modulator (not a generator) with full settings (mode, rate, octaves, gate, repeat); Shift has its settings too (amount ±36, scale Global/Off/named — degrees vs. chromatic), and the Delay — the first stateful time modulator — is in with rate, feedback, and a cumulative per-echo semitone shift. The global Quantize checkbox is gone; in its place Quantize is a timing modulator (rate grid + pair-based swing per the standards repo's `swing-timing.md`, LAM-style), pitch-snapping moved to a new Scale modulator (root/scale, default Global), and a Progression modulator walks a user-defined step list (degree I–VII + octave ±2 per step, 1–8 steps, bar-length rate) transposing everything through it. Two slow generators arrived after that: the Chord (a diatonic stack — degree I–VII, type Triad/7th/Sus2/Sus4/5th/6th, inversion — emitted on a bar-based Length/Repeat window) and the Drone (holds a voicing — root / root+5th / root+octave / triad, octave ±2 — on the same window model, defaulting to 4 bars, re-triggering immediately when its harmony changes mid-hold; bypasses Quantize and Delay by design). Their Length/Repeat pair joined the shared settings (the bar-length list, which gained a 16-bars entry). Every module in the palette now has a real settings dialog (`InlineDialog` grew same-row combo pairs, post-show add/remove, and non-closing utility buttons for the Progression's dynamic step rows). The plugin builds and runs on Linux (VST3 + Standalone); macOS/Windows/iPad targets are not yet ported. Phase 3 will be scoped after the user tests Phase 2. An end-of-Phase-2 architecture/code review lives in `code-review-2026-07.md`; its findings 1 (engine ignores host song position) and 2 (Standalone has no transport, so generators are silent) are now fixed, copying LAM's approach: every engine grid is re-derived each block from the host ppq position (no freewheeling counters — see "Transport and clocks" in `architecture.md`), and the Standalone/playhead-less hosts get a synthesized internal transport with a Play toggle and Tempo stepper in the menu bar. The remaining review findings (3 onward) are still open.

Module-settings UI redesign (in progress): the stacked-combo `InlineDialog`
settings dialogs are being replaced by a shared, structured `ModuleWindow`
(thin menu bar — Root / Scale / Rate-or-Length — over a 3x2 grid of combo/dial
cells, dials for knob-friendly values; see `architecture.md`). Random is the
first module converted and approved; the other twelve still open their
`InlineDialog` dialogs and are the pending rollout.

Note on phase numbering: the requirements were renumbered after the first coding session — what the phase-1 branch and its commits called "Phase 1: Canvas skeleton" is now Phase 2. Code comments follow the current numbering.

## Naming

"Current" is only a working title and cannot ship: Minimal Audio already has a product named Current. A different name must be chosen before release (add it to the shipping checklist work). Until then the repo, identifiers, and docs keep the working title — don't rename anything piecemeal.

## Pre-release

Current has not shipped: there are no users, so no saved DAW projects or presets exist outside development. Consequences: state and preset formats may change freely between sessions; never write migration shims or backward-compat load paths for older in-development formats (an old dev save loading with defaults is fine); don't hesitate to rename parameters, reorder option lists, or restructure persisted state when the design improves. This section comes out when the plugin ships (see `shipping-checklist.md` in the standards repo).

## Build instructions

- Linux: `./build_and_run_linux.sh` builds the VST3 + Standalone into `build-linux/`, installs the VST3 into `~/.vst3`, and launches the Standalone. Pass `--no-run` to build only (e.g. headless/CI — there's no display to open a window on). First configure fetches JUCE 8.0.12 via FetchContent, so it takes a few minutes; later builds are incremental.
- Linux build packages (Ubuntu/Debian). Every remote session should install these before the first build — fresh containers have the compiler and CMake but are missing the X11/GL/audio dev libraries, and the configure step then dies deep inside JUCE's juceaide build with a missing header like `X11/extensions/Xrandr.h`. Run `sudo apt-get update` first (stale package indexes cause 404s on install), then:
  `sudo apt-get install -y build-essential cmake ninja-build libasound2-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libxrender-dev libgl1-mesa-dev libfreetype-dev libfontconfig1-dev`
- If a configure attempt failed on missing packages, delete `build-linux/` before retrying — the half-configured CMake cache makes later builds fail with a confusing `ninja: error: loading 'build.ninja'`.
- Headless UI screenshots (no display in the container): additionally `sudo apt-get install -y xvfb xdotool imagemagick`, then start `Xvfb :99`, launch the Standalone with `DISPLAY=:99`, drive it with `xdotool` (mousemove/mousedown/mouseup for drags, `click --repeat 2` for the settings dialogs), capture with `import -window root out.png`. The palette/canvas coordinates can be computed from `MainView.h` layout constants plus the window position from `xdotool getwindowgeometry`.
- Engine smoke test: configure with `-DCURRENT_BUILD_TESTS=ON`, build, run `current_engine_test`. Run it after any engine change.
- macOS/Windows scripts (`build_and_run_mac.sh`, etc.) are not yet ported from Little Arp Monster; only Linux exists so far.

## Architecture

See `architecture.md`: component map, canvas-model ownership and persistence, the message-thread/audio-thread handoff, the fixed implicit engine chain and its defaults, the module-catalogue extension recipe, theming, and what's deferred to later phases.

## Module reference

See `modules.md`: manual-style docs for the implemented modules plus the planned catalogue, tiered into speced (codable as-is) and idea-only (open questions listed per module). Keep it updated as modules gain real settings or specs firm up.

## Known UI / settings inconsistencies (to fix in a later session)

A settings-consistency audit across all 13 modules' dialogs (2026-07) turned up the items below. None are shipped-user-facing yet (pre-release), so they can be fixed freely. Decisions marked "open" still need the user's call before coding; the rest are agreed. Nothing here is implemented.

**Before implementing any of these, hold a quick architecture discussion (user wants this looked at and considered).** The guiding product goal is that a given setting behaves *identically* across every module that has it, so the user always knows what to expect. The architecture question is how best to guarantee that in code: is it worth giving each shared setting its own single shared class / piece of code (the control, its option list, its persistence, its engine mapping) that every module merely *references*, rather than each module re-deriving the setting? This would make "same everywhere" structural rather than a convention to keep in sync by hand, and it dovetails with the `ModuleWindow` rollout's plan to give the shared-setting add/read helpers `ModuleWindow` counterparts. Weigh it against over-engineering (settings that look shared but have per-module edge cases — e.g. Rate-with-Gate on emitters vs. Rate-without-Gate on Quantize/Delay) before committing. Decide the approach with the user, then implement the items below on top of it.

- **Root/scale override — general model (AGREED 2026-07, applies to every pitch-mapping module).** Every module that maps pitch carries **both** a Root override and a Scale override — always the pair, never only one. Each offers the same set of choices: **Global** (the default for most modules — follow the plugin's global root/scale), **Off** (do not force the module's output onto a scale), or a specific named root/scale. This is the single behaviour all such modules must converge on; per-module asymmetries (Shift below, and any others found during the `ModuleWindow` rollout) get corrected to match it. Modules that legitimately don't map pitch (Arp, Quantize, MIDI In, Output) keep no root/scale at all — the general model applies only to modules that touch pitch. (Delay is now in scope for root/scale because of its per-echo shift — see the Delay item below.)

- **Shift Root override — RESOLVED (2026-07).** Shift adopts the general root/scale model above: it gains a Root override alongside its existing Scale, both offering Global / Off / named. Scale = Off means Shift moves in raw **semitones**; Scale on (Global or named) means Shift moves in **scale steps/degrees**. The chromatic "Off" behaviour is therefore kept — it is now just the "Scale = Off" case of the shared model, not a Shift special case. User feedback is required so the two modes are legible: the shift amount becomes a **dial** whose live label reads the active unit back, e.g. `Shift: 3 semitones` (Scale = Off) or `Shift: 3 steps` (Scale on). Implementation notes carried over: the `rootOverride` field already exists on `ModuleSettings`; the engine `Config` needs a new `shiftRoot` (there is `shiftScale`/`shiftAmount` but no root), populated in `PluginProcessor` next to `engShiftScale`, persisted alongside `shiftAmount`, and read as `cfg.shiftRoot >= 0 ? cfg.shiftRoot : root`.

- **Label collisions — the same word names different controls.** Three separate axes, resolved one at a time:
  - **Rate vs. Length — RESOLVED (2026-07).** These are two distinct timing flavours, and a module carries **exactly one of them, or neither** — never both.
    - **Rate** — short, repeating intervals from **1/32 to 1/1**, for modules that fire repeatedly (Scale gen, Random, LFO, Arp, Quantize, Delay). A **Rate control always carries a Gate control too** — Rate and Gate always ship as a pair (this pre-answers the "Gate exposed only on Arp" item below: any Rate module gets Gate).
    - **Length** — a single long note/window, for modules that naturally hold one long note (Chord, Drone). **No Gate** — the user shortens the note simply by picking a shorter Length.
    - The option lists must be **consistent across the whole app**: one canonical Rate list (1/32…1/1) everywhere Rate appears, one canonical Length list everywhere Length appears. (This subsumes the "Scale generator's Rate list starts at 1/16" item below.)
    - **Progression's timing — RESOLVED (2026-07):** its bar-length per-step cadence (currently mislabelled "Rate") is renamed **"Step Length"** — a distinct third label drawn from the shared bar-length list. "Rate" stays reserved strictly for the 1/32–1/1 note flavour. (Name is "for now"; revisit if a better word turns up.)
  - **"Repeat" — RESOLVED (2026-07): one meaning everywhere.** Repeat is the period (in bar-lengths, e.g. 1 bar / 4 bars; Endless/Off = never restart) after which a module **restarts from its start**, regardless of where it currently is — the scale walk jumps back to step 1, the arp restarts its pattern, and so on. It is **orthogonal to Rate/Length** and may sit alongside either, or be absent (Random and LFO legitimately still have none). On a Length module the two combine literally: a Chord with **Length 2 bars + Repeat 4 bars** sounds for 2 bars, then rests for 2 bars (the `Repeat − Length` remainder is silence), then re-triggers. This retires the old split meaning (note-length reset window on Scale/Arp vs. bar-length hold-repeat on Chord/Drone) — both become this single definition, and Chord/Drone keep both Length and Repeat with no conflict. Minor detail to settle in implementation: behaviour when Repeat < Length (presumably the note is cut at Repeat).
  - **"Octaves" vs "Octave" — RESOLVED (2026-07): keep both, no rename.** They name two genuinely different, conventional concepts and each is used consistently: **"Octaves"** (1–4 range span, on Scale gen and Arp) is the traditional arp/pattern span — how many octaves the walk repeats up through; **"Octave"** (−2…+2 transpose offset, on Drone and per-step in Progression) sets/shifts which octave the voicing sits in. Renaming would fight established language (an arp's "Octaves" span is standard), so both words stay. The near-identical spelling is disambiguated by the `ModuleWindow` live-label readout, e.g. Arp reads `Octaves: 2` (spans two octaves) while Drone reads `Octave: +1` (up one).

- **Gate exposure — RESOLVED (2026-07): Gate ships on every Rate module.** Follows directly from the Rate/Length rule above (Rate and Gate always ship as a pair). Scale gen and Random both have Rate, so both gain a real Gate control and the fixed-50% stub is retired. No Rate module is exempt.

- **Scale generator's Rate list starts at 1/16 — RESOLVED (2026-07) by the Rate-consistency rule above.** There is one canonical Rate list (1/32…1/1) everywhere Rate appears, so Scale gen's Rate simply extends down to 1/32 like every other Rate module.

- **Delay's per-echo Shift — RESOLVED (2026-07): give it Shift's root/scale model.** Delay's per-echo shift now maps pitch, so it adopts the general root/scale model (same as the Shift module): a **Root + Scale** pair, Global / Off / named. **Scale = Off** → the per-echo shift moves in raw **semitones** (today's behaviour, kept as the Off case); **Scale on** (Global or named) → the shift moves in **scale steps/degrees**. Because of this Delay now counts as a pitch-mapping module and drops off the "no root/scale" exemption list below.

Deliberate-and-fine (documented so a later session doesn't "unify" them by mistake): Arp / Quantize / I/O legitimately have no root/scale (Delay now does — see above); Random and LFO legitimately have no Repeat (Random is stochastic, LFO is cyclic by its Cycle length).

## TODO: roll the module window out to every module

Random is the only module on the redesigned `ModuleWindow` (menu bar + 3x2 grid; see `design/module-window.md`). The other twelve — Scale gen, LFO, Chord, Drone, Arp, Quantize, Scale mod, Progression, Shift, Delay, MIDI In, Output — still open their old stacked-combo `InlineDialog` dialogs and all need converting to the new window, one at a time, matching the layout rules in the design doc (dials for octaves/gate, live label readouts, Rate-vs-Length in the menu bar's third slot). As part of that rollout, give the shared-setting `add/read` helpers `ModuleWindow` counterparts so a shared control stays identical across modules, and retire `InlineDialog` once nothing uses it.
