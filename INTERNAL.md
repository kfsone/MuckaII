# MuckaII

FACT A MUD2-only client: a limited terminal emulator for the scroll, plus a command box that
nothing ever gets in the way of.

FACT Audience: the director and five or so people they know (`CLAUDE.md`, "no others"). Launch
means a GitHub release for them. Developers: LLM agents only. The director (Oliver "kfsone" Smith)
sets direction and does the acceptance testing.

## Stack

DECISION C++ with CMake: C++20 at minimum, or the highest standard the Windows, Linux and Android
toolchains all support (`docs/build.md` pins it). The director reads and spot-checks the code,
which makes the director a second reviewer. Rejected:
- Rust: with the director reading the code, C++ is the better choice.
- Go: no Slint binding; thin mobile GUIs.
- Python: the GIL hurts the latency budget; Android packaging is hard.

DECISION C++ memory safety is enforced by build gates, not policy: ASan and UBSan test runs in CI,
libFuzzer on the mud decoder seeded from `docs/evidence/captures/`, a fixed clang-tidy set banning
raw owning `new`/`delete` and C-style casts, and warnings as errors. Each gate's job and preset is
in `docs/build.md` (Presets, CI).

DECISION The GUI is Slint on every target, through its C++ API. Slint drives the Android soft
keyboard through a real Android input view; fades and float positions are its built-in property
animations. The input spike must confirm this choice. MuckaII contains no Rust; Slint's own build
needs one (`docs/build.md`). Rejected:
- Tauri and any JavaScript frontend: web views are weak at streaming text, and every line would
  cross a JS-to-native IPC hop.
- Flutter: adds Dart.
- egui: Rust, and its Android soft keyboard is unresolved upstream.

DECISION One library per layer, all in one process, no IPC; layers, threads and channels are in
`docs/spec/architecture.md`. Frontends (GUI, TUI, bot) subscribe to the session layer's event
stream in-process. The first frontend is a TUI on FTXUI: it proves the layering with no GUI risk.

DECISION The bot frontend exists for tests and for the director's future AI experiments against a
private MUD2 instance. It is not a player feature.
source: director

open: Is the bot built into release artifacts?
default: No. Test and developer presets only.

DECISION Sound uses miniaudio; the engine and playback rules are `docs/spec/architecture.md` (Audio)
and `docs/spec/sounds.toml`.

DECISION Latency is measured, not asserted: 30-50 ms from bytes arriving to the frame showing them,
and from FES arriving to the status bar repainted (`docs/spec/architecture.md`, Delivery).

## Platforms

DECISION Platform priority:
- Windows 11: primary; where the director plays.
- Android: second, one user. One release keystore, held in CI secrets.
- Linux and macOS: a distant third. Their release builds are not launch items.

The code is written for the director and one Android user.

DECISION The core libraries get a Linux CI job because UBSan and libFuzzer need clang on Linux. It
is gate infrastructure, not a product target.

DECISION iOS is deferred. The stack must not rule it out; no iOS work or CI until the director asks.

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
- the tombstone screen
- command understanding and probe injection (`docs/spec/session.md`)

Everything else is triaged after launch and re-justified against `docs/spec/fair-play.md` before it
is built: combat panel, side panel, floating widgets, compass, watchwords, anti-idle keepalive,
keep-screen-on, persona deletion, wire-log retention policy, the score graph (until it exists the
tombstone's name link is inert), `$LASTSAW` and `$FETCH`, output reformatters.

## Data

DECISION Runtime data lives in `~/.muckaii/`. On first run, settings are imported once from Mucka's
`mucka.ini`, opened read-only: profiles in MRU order, settings, F-keys, and `[watch]` verbatim. On
desktop the file is found automatically; everywhere, including Android where Mucka's ini is
app-private, a file picker is offered. Passwords are not importable; the user enters each once, the
first time that profile is used. Mucka's `mucka.db` is not imported.

DECISION The wire log keeps every byte verbatim except the outbound password and account ID, each
replaced with same-length placeholder bytes and annotated (`docs/spec/persistence.md`). Rejected:
keeping them verbatim (Mucka's behaviour).

Open protocol and telnet questions are settled by the evidence ranking in `CLAUDE.md`.
