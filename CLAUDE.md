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

- **Shift has a Scale override but no Root** — the one real gap. Its sibling modulators (Scale mod, Progression) both offer the shared root+scale pair and honour a per-module root; Shift instead hardcodes the *global* root in the engine (`Engine::mapPitch`, Shift branch), so it can only ever walk the current key's degrees. Agreed fix: give Shift a Root override behaving exactly like the siblings (Global default, resolved per block). The `rootOverride` field already exists on `ModuleSettings`; the engine `Config` needs a new `shiftRoot` (there is `shiftScale`/`shiftAmount` but no root), populated in `PluginProcessor` next to `engShiftScale`, persisted alongside `shiftAmount`, and read as `cfg.shiftRoot >= 0 ? cfg.shiftRoot : root`. Open: keep Shift's chromatic "Off" scale entry (shift in raw semitones) when adding Root, or drop it for a pure sibling-match. Leaning keep.

- **Label collisions — the same word names different controls.** "Rate" is the note-length grid (1/32…1/1) on Random/Scale/LFO/Arp/Quantize/Delay but a bar-length (1/4 bar…16 bars) on Progression. "Repeat" is the note-length reset window (Endless…4 bars) on Scale/Arp but the bar-length hold-repeat on Chord/Drone. "Octaves" (1–4 range span, Scale/Arp) reads almost identically to "Octave" (±2 transpose offset, Drone; also per-step in Progression). Open: rename to disambiguate (leaning this) vs. leave the labels and only document the distinction.

- **Gate is exposed only on Arp** — other stepped emitters (Scale gen, Random) run a fixed 50% (the code calls this a stub "until they grow the control"). Open: leave Arp-only and document as intentional (leaning this) vs. expand Gate to the stepped emitters.

- **Scale generator's Rate list starts at 1/16** (no 1/32), unlike every other Rate user — a minor asymmetry in the control's range.

- **Delay's per-echo Shift is chromatic-only** (semitones), whereas the Shift module can move in scale steps — an asymmetry, arguably a feature rather than a bug.

Deliberate-and-fine (documented so a later session doesn't "unify" them by mistake): Arp / Quantize / Delay / I/O legitimately have no root/scale; Random and LFO legitimately have no Repeat (Random is stochastic, LFO is cyclic by its Cycle length).
