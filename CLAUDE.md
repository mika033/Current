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

## Status

Not yet started. No code, build scripts, or UI decisions have been made. This file currently holds only the shared cross-plugin principles copied from SnorkelAudioStandards; plugin-specific sections (architecture, build instructions, naming glossary, UI conventions) will be added here as those decisions are made with the user.
