# MuckaII

FACT MuckaII is a MUD2-only client: a limited terminal emulator for the scroll (ANSI SGR colour and
clear screen, no cursor addressing), plus a command box that owns the keyboard (`CLAUDE.md`,
Invariants #0 and #1).

FACT Audience: the director and five or so people they know (`CLAUDE.md`, "no others"). MuckaII is
an open source project, not a product. Developers: LLM agents only. The director (Oliver "kfsone" Smith) sets direction and does the
acceptance testing.
source: director

FACT Mucka is the previous client. It reaches this project only through `docs/spec/` (`CLAUDE.md`,
The reference firewall).

What any feature may do is `docs/spec/fair-play.md`, which outranks every feature (`CLAUDE.md`, Fair
play).

## Stack

DECISION Latency budget, a target: 30-50 ms from bytes arriving to the frame showing them, and from
FES arriving to the status bar repainted. The spike measures it (`docs/spec/architecture.md`,
Delivery); it is never asserted.

DECISION C++20 with CMake (`docs/build.md`). Rejected: C++23 (no stable MSVC `/std:c++23`; NDK
libc++ gaps). The director spot-checks the code; write for a 40-year C++ reader. Rejected languages:
- Rust: with the director reading the code, C++ is the better choice.
- Go: no Slint binding; thin mobile GUIs.
- Python: the GIL hurts the latency budget; Android packaging is hard.

DECISION C++ memory safety is enforced by build gates, not policy: ASan and UBSan test runs in CI,
libFuzzer on the mud decoder seeded from `docs/evidence/captures/`, a fixed clang-tidy set banning
raw owning `new`/`delete` and C-style casts, and warnings as errors. Each gate's job and preset is
in `docs/build.md` (Presets, CI).

DECISION The GUI is Slint, through its C++ API, on Windows only for now. Fades and float positions
are Slint's built-in property animations. Slint can also drive the Android soft keyboard through a
real Android input view, which keeps the later Android GUI open (Platforms). MuckaII contains no
Rust; Slint's own build needs one (`docs/build.md`). Rejected:
- Tauri and any JavaScript frontend: web views are weak at streaming text, and every line would
  cross a JS-to-native IPC hop.
- Flutter: adds Dart.
- egui: Rust, and its Android soft keyboard is unresolved upstream.

DECISION The spike is the first build of the GUI input path on Windows. It confirms the Slint
choice and passes when, on the target, Invariants #0 and #1 hold (`CLAUDE.md`) and the latency
budget above is met, measured as `docs/spec/architecture.md` Delivery states.

DECISION One library per layer, all in one process, no IPC; layers, threads and channels are in
`docs/spec/architecture.md`. Frontends (GUI, TUI, bot) subscribe to the session layer's event
stream in-process. The first frontend is a TUI on FTXUI: it proves the layering with no GUI risk.

DECISION `muckaiiTui` is a separate artifact for Windows, Linux and macOS. The GUI is Windows only
for now.
source: director

DECISION `muckaiiBot` is a test frontend over the same core, built only in test and developer
presets. It never connects to the public game. Behaviour beyond the test harness waits for a
private server with Bartle's permission.
source: director

DECISION Sound uses miniaudio; the engine and playback rules are `docs/spec/architecture.md` (Audio)
and `docs/spec/sounds.toml`.

## Platforms

DECISION Platforms now:

| Platform | Artifacts | Tested by |
|----------|-----------|-----------|
| Windows 11 | the GUI (Slint) and the TUI | the director, who plays there |
| Linux | the TUI | the director, under WSL on the director's machine |
| macOS | the TUI | the director, when it becomes relevant |

No other platform or setup gets compatibility work (`CLAUDE.md`, "no others").
source: director

DECISION Android is `scope: later`: the GUI on Android. The `android` worktree's spike continues on
its own; its build notes are in `docs/build.md`.
source: director

DECISION The core libraries get a Linux CI job because UBSan and libFuzzer need clang on Linux. It
is gate infrastructure.

DECISION iOS is deferred. The stack must not rule it out; no iOS work or CI until the director asks.

## Scope

DECISION Now is the first usable Windows build. This is the single scope list; a spec's `scope:`
tag follows it. Now:

- the profile list, the profile page and per-profile advanced settings, editable without logging
  in (`docs/spec/flow.md`, Profile)
- global settings, with Apply / Save / Save as default (`docs/spec/flow.md`, Settings)
- the pre-game, game and post-game flow, and disconnecting back to Profile (`docs/spec/flow.md`)
- managed persona selection, and creating a persona in an open slot
- detecting the end of the game (the return to the Option menu)
- sleep tracking, so the status bar does not go stale (`docs/spec/session.md`)
- the raw console (`$con`), in the GUI and the TUI (`docs/spec/input.md`)
- terminal and scrollback
- command box and history
- profiles and auto-login
- command-line arguments (`docs/spec/connection.md` 6)
- F-keys (all three layers) and aliases, including the `^1`-`^3` slots
- Ctrl+D and Ctrl+Shift+D dreamword, Ctrl+F and Ctrl+Shift+F flee, Ctrl+R reply to last tell
- chat colouring (say/shout/tell/emote, own vs others)
- wire log
- status bar and dreamword
- sounds
- first-run import of Mucka's `mucka.ini`
- the tombstone screen
- command understanding and probe injection (`docs/spec/session.md`)

DECISION Everything under `docs/spec/later/` is later, and so is anything a now-scope spec tags
`scope: later` in place. A later item is re-justified against `docs/spec/fair-play.md` before it is
built.

RULE When the director rules an `docs/spec/OPEN.md` E item, this list changes in the same commit.
enforced_by: none: judgement

## Data

DECISION Runtime data lives in `~/.muckaii/` (`docs/spec/persistence.md`). On first run, settings
are imported once from Mucka's `mucka.ini`, opened read-only: profiles in MRU order, settings,
F-keys, and `[watch]` verbatim. The file is found automatically where it can be, and a file picker
is offered. Passwords are not importable. Mucka's `mucka.db` is not imported. Acceptance: a user
with an existing `mucka.ini` reaches a populated profile list and enters each password once.

DECISION The wire log keeps every byte verbatim except the outbound password and account ID, each
replaced with same-length placeholder bytes and annotated (`docs/spec/persistence.md`). Rejected:
keeping them verbatim (Mucka's behaviour).

## Credentials and threat model

FACT MUD2 accounts are free. They hold no personally identifying data apart from a recovery email
address. MUD2 itself sends everything, the password included, over unencrypted telnet. Credential
handling in MuckaII is sized to that: keep the password out of the wire log and the recall history,
and do nothing more elaborate.
source: director

DECISION A stored profile password is kept in plaintext in the profile settings
(`docs/spec/persistence.md`, Settings storage). Rejected: an OS secure store.
source: director

DECISION Passwords are expected through the command line or a stored profile, not typed; a typed
password is visible as it is entered. Password fields: `docs/spec/input.md`, Password entry.
source: director
