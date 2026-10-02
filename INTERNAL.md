# MuckaII

FACT A MUD2-only client. In essence it is a limited terminal emulator for the scroll, plus a command
box that nothing ever gets in the way of.

FACT Audience: the director and five or so people they know. Launch means a GitHub release for them.

FACT Developers: LLM agents only. The director (Oliver "kfsone" Smith) sets direction and does the
acceptance testing.

## Stack

DECISION The language is C++, built with CMake. The standard is C++20 at minimum, or the highest
standard that the Windows, Linux and Android toolchains all support. `docs/build.md` pins it. The
director reads and spot-checks the code, and that makes the director a second reviewer. Rejected:
- Rust. With no human reading the code, its compiler would be the only reviewer. With the director
  reading it, C++ is the better choice.
- Go. It has no Slint binding and its mobile GUIs are thin.
- Python. The GIL hurts the latency budget, and packaging for Android is hard.

DECISION C++ memory safety is enforced by build gates, not by policy:
- ASan and UBSan test runs in CI.
- libFuzzer on the mud decoder, seeded from `docs/evidence/captures/`.
- A fixed clang-tidy check set that bans raw owning `new`/`delete` and C-style casts.
- `-Werror`.

Each gate is named in `CLAUDE.md` once it exists.

DECISION The GUI is Slint on every target, through its C++ API. Slint drives the Android soft
keyboard through a real Android input view. Fades and float positions are its built-in property
animations. The input spike has to confirm this choice. Slint's runtime is written in Rust, so
building Slint from source needs a Rust toolchain on the build machine. MuckaII itself contains no
Rust (`docs/build.md`). Rejected:
- Tauri and any JavaScript frontend. Streaming text is a known weakness of web views, and every
  line would cross a JS-to-native IPC hop.
- Flutter, which would add Dart.
- egui as the GUI everywhere. Its Android soft keyboard is unresolved upstream. It stays as the
  desktop fallback.

DECISION The architecture is layered, as one library per layer, all in one process with no IPC:

| Layer | Owns |
|-------|------|
| transport | TCP and telnet negotiation |
| mud | bytes in, `docs/spec/events.md` events out |
| session | login, probes and probe-block insertion, stats, sleep, dreamword, flow state, the single ordered outbound queue |
| client | aliases, F-key macros, history, parsing command boundaries, mapping keys to commands |

Frontends (the GUI, a TUI, a bot) subscribe to the session layer's event stream in-process. The
first frontend is a TUI built on FTXUI, because it proves the layering without any GUI risk.
`docs/spec/architecture.md` pins the threads and interfaces.

DECISION The bot frontend exists for tests and for the director's future AI experiments against a
private MUD2 instance. It is not a player feature.
source: director

open: Is the bot built into release artifacts?
default: No. It is built only in test and developer presets.

DECISION Sound uses miniaudio:
- One output stream is opened at startup and stays open.
- Every sound is decoded to PCM when it loads.
- Playing a sound adds a voice to the mixer, so overlapping sounds work.
- Every play request carries a start time on the audio clock.

Rejected: padding sounds with leading silence to hide player start-up latency. That is Mucka's
workaround, and it puts the sound late.

DECISION The latency budget runs from bytes arriving to the frame showing that text, and from FES
arriving to the status bar repainted. The target is 30-50 ms for both, measured by a test rather
than asserted.

## Platforms

DECISION Platform priority:
- Windows 11 is primary. It is where the director plays.
- Android is second, with one user. It uses one release keystore, held in CI secrets.
- Linux and macOS are a distant third. Their release builds are not launch items.

The audience will never be significant, so the code is written for the director and one Android
user.

DECISION The core libraries still get a Linux CI job, because UBSan and libFuzzer need clang on
Linux. That job is gate infrastructure, not a product target.

DECISION iOS is deferred. The stack must not rule it out, but there is no iOS work and no iOS CI
until the director asks.

## Scope

DECISION The launch scope is:

- the profile page, pre-game, game and post-game flow (`docs/spec/flow.md`)
- managed persona selection, and creating a persona in an open slot
- detecting the end of the game (the return to the Option menu)
- sleep tracking, so the status bar does not go stale (`docs/spec/session.md`)
- the raw console (`$con`) on every platform, an early feature (`docs/spec/input.md`)
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

Everything else is triaged after launch and re-justified against `docs/spec/fair-play.md` before it
is built: combat panel, side panel, floating widgets, compass, watchwords, anti-idle
keepalive, keep-screen-on, persona deletion, and wire-log retention policy.

DECISION These are launch scope:
- the tombstone screen;
- command understanding and probe injection (`docs/spec/session.md`).

These are later:
- the score graph (until it exists, the tombstone's name link is inert);
- `$LASTSAW` and `$FETCH`;
- output reformatters.

## Data

DECISION Runtime data lives in `~/.muckaii/`. On first run, settings are imported once from
Mucka's `mucka.ini`, which is opened read-only. The import covers profiles in MRU order, settings,
F-keys, and `[watch]` verbatim. On desktop the file is found automatically. Everywhere, including
Android where Mucka's ini is app-private, a file picker is offered. Passwords are not importable;
the user enters each one once, the first time that profile is used. Mucka's `mucka.db` is not
imported.

DECISION The wire log keeps every byte verbatim except the outbound password and account ID. Each
is replaced with placeholder bytes of the same length and carries an annotation
(`docs/spec/persistence.md`). Rejected: keeping them verbatim (Mucka's behaviour).

DECISION Open protocol and telnet questions are settled by the evidence ranking in `CLAUDE.md`:
Bartle first, two or more independent proofs to deprecate a Bartle statement, then captures, Clio,
and the reference. Each settlement is recorded as a DECISION in the spec it changes. Where the
ranking does not decide, the item stays open with a default.
