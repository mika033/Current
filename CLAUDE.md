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

## Status

Phase 1 (canvas skeleton) is implemented, per `generative-midi-plugin-requirements.md`. The plugin builds and runs on Linux (VST3 + Standalone); macOS/Windows/iPad targets and the generative engine are not yet done. Phase 2 has deliberately not been touched.

## Build instructions

- Linux: `./build_and_run_linux.sh` builds the VST3 + Standalone into `build-linux/`, installs the VST3 into `~/.vst3`, and launches the Standalone. Pass `--no-run` to build only (e.g. headless/CI — there's no display to open a window on). First configure fetches JUCE 8.0.12 via FetchContent, so it takes a few minutes; later builds are incremental.
- Linux dev deps (Ubuntu): `libasound2-dev`, `libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev libxrender-dev`, `libgl1-mesa-dev`, `libfreetype-dev libfontconfig1-dev`.
- macOS/Windows scripts (`build_and_run_mac.sh`, etc.) are not yet ported from Little Arp Monster; only Linux exists so far.

## Phase 1 architecture

Fresh, minimal JUCE plugin. The self-contained `Theme` (two-scheme Light/Dark palette) and `InlineDialog` helpers are adapted from Little Arp Monster; everything else is new to Current.

- `CurrentAudioProcessor` — a MIDI-effect skeleton. No generative engine yet: `processBlock` clears audio and passes MIDI through untouched. APVTS holds the global settings (`root`, `scale`, `quantize`) plus a `theme` param. It also owns the **canvas model** — a `std::vector<ModuleInstance>` (type + x/y) — which is serialized in the DAW project state (a `<Canvas>` child of the APVTS tree). This is state persistence, NOT the user-facing Load/Save, which Phase 1 omits. The model is message-thread only for now; when an engine lands and `processBlock` starts consuming the graph, that handoff needs revisiting.
- `CurrentAudioProcessorEditor` — derives from `DragAndDropContainer` (must be an ancestor of both the palette and the canvas). Owns the `CurrentLookAndFeel`, hosts `MainView`, and provides `showInlineDialog()` + `applyTheme()`.
- `MainView` — lays out `MenuBar` (top), `Canvas` (middle), `PaletteBar` (bottom).
- `MenuBar` — global-settings combos/toggle bound to the APVTS, plus the Theme switch. Edit / Load-Save menus are deferred.
- `Canvas` — the `DragAndDropTarget` drop surface. Rebuilds nodes from the processor model, writes adds/moves/removes back to it, and opens the settings placeholder on a node's double-click.
- `ModuleComponent` — a draggable node. Generators paint as squares, modulators as circles; colour encodes the family (see `Theme` genFill/modFill/ioFill). Ports are drawn but decorative — wiring is a later phase.
- `PaletteBar` / `ModuleTypes` — the Phase 1 catalogue: generators **Arp**, **Random**; modulators **Quantize**, **Shift**. New modules slot in by extending the `ModuleType` enum + `moduleCatalogue()`.

Deferred to later phases (out of scope for Phase 1): real module DSP, port wiring / connections, pan & zoom, marquee/multi-select, undo/redo, and user preset Load/Save.
