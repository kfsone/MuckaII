# MuckaII

FACT A MUD2-only client. In essence it is a limited terminal emulator for the scroll, plus a command
box that nothing ever gets in the way of.

FACT Audience: the director and five or so people he knows. Launch means a GitHub release for them.

FACT Developers: LLM agents only. The director (Oliver "kfsone" Smith) sets direction and does the
acceptance testing.

## Stack

DECISION The core is Rust: a pure library crate `mud2` with no UI dependency. It holds telnet, the C1
and ANSI decoding, the line buffer and wrap, login, the 7-bit send path, and the wire log
(SQLite). Rejected: C++/Qt (mobile licensing, toolchain weight, and memory-safety review with no
human reviewer) and Go (thin GUI options for mobile).

DECISION The shell candidate is Tauri v2 with a TypeScript frontend. It is a hypothesis until the
input spike passes. The fallbacks are egui for desktop and a native Kotlin `EditText` shell over the
core for Android. Rejected: Slint and Fyne/Gio (thin in training data, the same failure mode as
MAUI) and Flutter (a second language, no gain).

## Platforms

DECISION Targets for launch: Windows 11, Android, Linux, macOS. macOS builds are ad-hoc signed and
not notarised. Android builds use one release keystore, held in CI secrets.

DECISION iOS is deferred. The stack must not rule it out, but there is no iOS work and no iOS CI
until the director asks.

## Scope

DECISION The launch scope is:

- terminal and scrollback
- command box and history
- profiles and auto-login
- F-keys (all three layers) and aliases, including the `^1`-`^3` slots
- Ctrl+D and Ctrl+Shift+D dreamword, Ctrl+F and Ctrl+Shift+F flee, Ctrl+R reply to last tell
- chat colouring (say/shout/tell/emote, own vs others)
- wire log
- status bar and dreamword
- sounds
- first-run import of Mucka's `mucka.ini`

Everything else is triaged after launch and re-justified before it is built: combat rail, side
panels, compass, score graph, guided login, raw console, watchwords, anti-idle keepalive, and
keep-screen-on.

## Data

DECISION Runtime data lives in `~/.muckaii/`. On first run, settings are imported once from
Mucka's `mucka.ini`, which is opened read-only. The import covers profiles in MRU order, settings,
F-keys, and `[watch]` verbatim. On desktop the file is found automatically. Everywhere, including
Android where Mucka's ini is app-private, a file picker is offered. Passwords are not importable;
the user enters each one once, the first time that profile is used. Mucka's `mucka.db` is not
imported.

DECISION The wire log keeps every byte verbatim except the outbound password line. That line is
replaced with placeholder bytes of the same length and carries an annotation. Rejected: keeping
the password verbatim (Mucka's behaviour).

DECISION Open protocol and telnet questions (`docs/spec/OPEN.md` groups B and C) are settled by
the evidence ranking in `CLAUDE.md`: Bartle, then captures, then Clio. The reference is used only
where none of those speaks. Each settlement is recorded as a DECISION in the spec it changes.
