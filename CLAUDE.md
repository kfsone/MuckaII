# Working on MuckaII

FACT MuckaII is a MUD2 client, written from scratch. `INTERNAL.md` says what the project is. This
file says what counts as knowledge here and states the invariants that outrank everything else.

RULE Read this file before `INTERNAL.md`.
enforced_by: none: judgement

Every governing statement carries a tag:

| Tag | Is |
|-----|----|
| FACT | how the world is. Never an instruction |
| RULE | do, or do not, within a stated scope. Ends with `enforced_by:` naming the gate that fails the build, or `none: judgement` |
| DECISION | what we chose, and the rejected option by name so it does not return as a new idea |
| EVIDENCE | an observation: wire fact, captured line, measurement with its conditions, quotation. Verbatim |

In `docs/spec/` every statement also carries `source:` (`docs/spec/README.md`).

## The far end

FACT MUD2 dates from the 1980s and is frozen. Its wire format is MUD2's own encoding, not ASCII:
printable 7-bit text, with bytes 0x9B-0xFE leading nested code sequences, each ending `FF FF`, that
bracket and label the text like markup tags. Bytes 0x80-0x9A are not markup
(`docs/spec/protocol.md`, Control characters). Telnet (IAC, 0xFF) sits underneath. On top the
server sends a small ANSI subset: colour (SGR) and clear-screen, no cursor positioning. The client
sends printable 7-bit text, the client-mode escape sequences it adds, and telnet negotiation.
source: bartle:FE4; docs/spec/protocol.md

RULE Never treat the wire as text. It is bytes, decoded by `docs/spec/protocol.md`. Never
transcode, strip, or replace a byte on the way in. Decide every byte from the spec, not from a text
encoding.
enforced_by: none: judgement

FACT Persona names are public in the game and are not sensitive. Account IDs and passwords are.
source: director

RULE Never mask persona names in evidence. Always mask account IDs and passwords.
enforced_by: none: judgement

FACT A missing feature in MUD2 is part of its shape, not a gap to work around. Its design is
deliberate far more often than naive.

RULE Assume intent before error. Never assume the prompt is `*`: it is a bracketed code sequence.
enforced_by: none: judgement

RULE Do not design for a MUD2 that changes. No plugin hooks, no configurable protocol tables, no
abstraction over protocol variation that cannot happen.
enforced_by: none: judgement

FACT There are no others on this project. Only the director's machine and GitHub CI build it. Only
the director and a handful of people they know play it.
source: director

RULE Support the pinned toolchain alone. No compatibility for older compilers or other setups, no
contributor docs, no hedging for hypothetical users.
enforced_by: none: judgement

FACT There is no test server. Every interaction with MUD2 is a live game with real players. The
captures and the wire corpus are the only safe place to experiment.

## Fair play

RULE `docs/spec/fair-play.md` outranks every feature. Every feature spec names the charter line that
permits it; a feature that cannot goes to the director before it is designed. Features are
secretaries in the wings, not super powers.
enforced_by: none: judgement

## Evidence, strongest first

1. Bartle's documentation: `G:\Source\mud-fe\mud2_FE4.txt` (newer; governs where the two differ)
   and `G:\Source\mud-fe\MUD-FECodes.txt`. Local paths only, never committed.
2. Bartle's own words on a specific question, quoted verbatim in `docs/evidence/`.
3. Captured wire traffic: `docs/evidence/captures/*.c1`.
4. `G:\Source\clio-1.8a`: a working human-written client. First opinion on mechanics; no authority
   over Bartle.
5. The previous client's behaviour, extracted into `docs/spec/` as `source: reference`. Used only
   where nothing above speaks.

DECISION Bartle comes first, though his documents may predate the newest server code. Deprecating a
Bartle statement takes two or more independent proofs (such as captures from different sessions).
One disagreeing capture is an `open:` question, not a ruling. Where Bartle is silent:

| Case | Outcome |
|------|---------|
| a capture speaks | the capture decides |
| no capture; the remaining sources agree | that agreement decides |
| no capture; the remaining sources split | open, with a `default:`, for the director |

Each settlement is a DECISION in the spec it changes, citing this rule and the deciding source; its
`docs/spec/OPEN.md` row is deleted. Rejected: ranking captures above Bartle.
source: director

RULE Evidence is immutable and outranks the code. If they disagree, the code is wrong.
enforced_by: none: judgement

RULE Trust observations; verify every claim about mechanism before building on it. The corpus here
is written by AI. A model meeting its own earlier output is not meeting a colleague.
enforced_by: none: judgement

## The reference firewall

FACT `G:\Source\mucka` is the previous .NET/MAUI client. It reaches this project only as its
behaviour extracted into `docs/spec/`.

RULE Never read, search, or port from `G:\Source\mucka`, its install directory, or its GitHub repo.
If the spec is silent, the answer comes from evidence or the director. Extraction is finished; no
exception.
enforced_by: .claude/hooks/reference-firewall.mjs, .claude/settings.json permissions.deny

FACT The hook runs before every tool call and blocks:
- any path component named `mucka`;
- the bare `G:\Source` directory, and wildcards directly under it;
- the tripwire list;
- any agent write to `.claude/settings*.json` or `.claude/hooks/`.

A block is the firewall working, not a bug to work around. The director edits the firewall files
by hand. Name the sibling directory you need (`MuckaII`, `mud-fe`, `clio-1.8a`); never search
`G:\Source`.

FACT The firewall applies only to sessions started in this worktree. Its script logic is tested; a
live session is not yet proven. Director task: in a fresh session in `G:\Source\MuckaII\main`,
confirm that Bash and Read of the previous client's CLAUDE.md are blocked, and a subagent too.

FACT `scripts/firewall/reference-identifiers.txt` lists distinctive reference type names. Agents
never read it. `scripts/check-tripwire.py` reads it and fails if any of those names appears
elsewhere in the tree.

## Where things are

| What | Where |
|------|-------|
| Which spec each layer answers to | `docs/spec/README.md` |
| Layers, threads, event delivery, outbound queue | `docs/spec/architecture.md` |
| Toolchain, presets, dependencies, CI | `docs/build.md` |
| C++ style | `docs/style.md`, enforced by `.clang-format` and `.clang-tidy` |
| Code | `src/<layer>/`; public headers in `src/<layer>/include/muckaii/<layer>/` |
| Repo gates | `scripts/check-all.py` (captures, 7-bit text, tripwire, spec tags, goldens, C1 code table, format) |
| Lint gate | `scripts/check-tidy.py`, run by ctest |
| Layer direction gate | `cmake/` (checked at configure) and a ctest include check |

FACT `cmake --preset windows-msvc-debug`, `cmake --build --preset windows-msvc-debug`,
`ctest --preset windows-msvc-debug` builds and runs the unit tests and every repo gate. Every RULE
that names a gate above is enforced by it.

## Invariant #0: the command input owns the keyboard

RULE After any tap, click, drag, or toggle, keyboard focus is in the command input. The only
exception is while another real text field is open; focus returns when it closes. Enforce this in
one place by structure, not with a list of controls.
enforced_by: none: judgement

open: On Android, does a tap or drag on the scroll raise the soft keyboard?
default: Focus stays in the command input and the soft keyboard is left as it was. A scroll gesture
never raises it.

## Invariant #1: one ordered stream, keyboard to socket

EVIDENCE The director types at 120-130 wpm.

RULE Typed text and Enter form one ordered stream, all the way to the socket. Checked at two points:
1. **Frontend to client.** The text handed to the client at each Enter is exactly the frontend's
   line buffer when that Enter was applied. Concatenated, those texts equal the typed keystroke
   stream.
2. **Client to socket.** The client's documented processing follows (local `$` commands, alias and
   macro expansion, the 7-bit filter); its output reaches the socket complete and in order. The
   only additions are probe blocks the session layer inserts at command boundaries, never inside a
   command, each marked in the wire log. With the marked blocks removed, the bytes equal the
   client's output.

- The frontend owns the line buffer and applies input events in arrival order. Enter acts exactly
  where it falls in that order. No widget applies a batch of keystrokes before Enter is examined.
  Nothing sits between the widget and the read: no binding, no dispatcher hop. "The box's text" in
  any spec, Ctrl+Shift+D and Ctrl+Shift+F included, means this buffer when the key is applied,
  never a widget property read later.
- Where the platform delivers IME commits (Android), take the committed text; never rebuild it
  from key events.
- Every outbound line, typed or macro, goes through one ordered channel to one socket writer
  (`docs/spec/architecture.md`, `docs/spec/session.md`).
- Nothing on the UI thread may stall typing, even for 50 ms.

enforced_by: none: judgement

## Bytes, not strings

RULE The protocol layer takes and returns bytes. Lossy text decoding is never used there.
enforced_by: none: judgement

RULE What the player types is reduced to printable 7-bit characters at the single send boundary:
typographic punctuation transliterated, anything else dropped. The client's own escape sequences
(`ESC-[ ... ESC-]`) are added after this step and never filtered. The raw console sends verbatim
and bypasses it.
enforced_by: none: judgement

## Comments and docs

RULE A comment records what was observed, or a rule the director set. No dates, no changelog, no
theories about the server, no prose forbidding future work. Git holds the history.
enforced_by: none: judgement

RULE A spec states requirements in the present tense ("MuckaII sends ..."), built or not. A work
order (plan, migration list, "still to do") is deleted when its work lands. Name symbols, never
line numbers. A number travels with the name that owns it.
enforced_by: none: judgement

RULE MUD2 documentation is stored as data tables in `docs/spec/`, not prose.
enforced_by: none: judgement

RULE The repo's own text files (source, docs, config, scripts) are 7-bit; a glyph the UI needs is
written as its code point. Exempt: the captures (`*.c1`, the server's bytes verbatim) and binary
assets. This is about our files, not the wire.
enforced_by: scripts/check-text-files.py (via scripts/check-all.py)

## Tests

RULE Prove a test can fail: break the code on purpose, confirm the break applied, watch the test
fail, restore the code.
enforced_by: none: judgement

RULE Say plainly what cannot be tested from here. For UI behaviour, the gate is the director's
hands.
enforced_by: none: judgement

## Git

RULE Commit only on request. Pushing is a separate request.
enforced_by: none: judgement

RULE The repo is bare at `G:\Source\MuckaII`, with worktrees beneath it; a worktree's directory name
equals its branch. Never create a branch unless asked and told which worktree it belongs to.
enforced_by: none: judgement
