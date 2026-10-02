# Working on MuckaII

FACT MuckaII is a client for MUD2, written from the ground up. `INTERNAL.md` says what the project
is. This file says what kind of knowledge counts here and states the invariants that outrank
everything else.

RULE Read this file before `INTERNAL.md`.
enforced_by: none: judgement

Every governing statement carries a tag:

- **FACT**: how the world is. Never an instruction.
- **RULE**: do, or do not, within a stated scope. It ends with `enforced_by:`, naming either the
  gate that fails the build or `none: judgement`.
- **DECISION**: what we chose and what we rejected. The rejected option is named so it does not
  come back as a new idea.
- **EVIDENCE**: something observed, such as a wire fact, a captured line, a measurement with its
  conditions, or a quotation. Verbatim.

## The far end

FACT MUD2 dates from the 1980s and is frozen. Its wire format is not ASCII. It is MUD2's own
encoding: printable 7-bit text, with bytes 0x9B-0xFE used as markup. Those bytes lead nested code
sequences, each ending `FF FF`, that bracket and label the text, much as tags do in a markup
language. Bytes 0x80-0x9A are not markup (`docs/spec/protocol.md`, Control characters). Telnet
negotiation (IAC, 0xFF) sits underneath all of this. On top of it the server sends a small ANSI
subset: colour (SGR) and clear-screen, with no cursor positioning. What the client sends is
printable 7-bit text, the client-mode escape sequences the client adds, and telnet negotiation.
source: bartle:FE4; docs/spec/protocol.md

RULE Never treat the wire as text. It is bytes to decode by `docs/spec/protocol.md`. Never
transcode, strip, or replace a byte on the way in. Decide what to do with every byte from the spec,
not from a text encoding.
enforced_by: none: judgement

FACT Persona names are not sensitive. They are public in the game and give nobody grounds to break
into an account. Account IDs and passwords are sensitive. Never mask persona names in evidence;
always mask account IDs and passwords.
source: director

FACT A missing feature in MUD2 is part of its shape, not a gap to work around. Its design is
deliberate far more often than it is naive.

RULE Assume intent before assuming error. Never assume the prompt is `*`. The prompt is a bracketed
code sequence.
enforced_by: none: judgement

RULE Do not design for a future in which MUD2 changes. It will not change. No plugin hooks, no
configurable protocol tables, no abstraction over protocol variation that cannot happen.
enforced_by: none: judgement

FACT There is no test server. Every interaction with MUD2 happens in a live game with real players.
The captures and the wire corpus are the only safe place to experiment.

## Fair play

RULE `docs/spec/fair-play.md` outranks every feature. Every feature spec names the charter line that
permits it. A feature that cannot name one goes to the director before it is designed. The
features are secretaries in the wings, not super powers.
enforced_by: none: judgement

## Evidence, strongest first

1. Bartle's documentation: `G:\Source\mud-fe\mud2_FE4.txt` (newer; it governs where the two differ)
   and `G:\Source\mud-fe\MUD-FECodes.txt`. These are local paths only and never committed.
2. Bartle's own words on a specific question, quoted verbatim in `docs/evidence/`.
3. Captured wire traffic: the `.c1` captures in `docs/evidence/captures/`.
4. `G:\Source\clio-1.8a`: a working client written by a human. It is the first opinion on
   mechanics, but it has no authority over Bartle.
5. The previous client's behaviour, as extracted into `docs/spec/` with `source: reference`. It is
   used only where nothing above speaks.

DECISION Bartle comes first, even though his documents may be older than the newest server code.
Deprecating a Bartle statement takes two or more independent proofs, such as captures from
different sessions. One capture that disagrees with Bartle is recorded as an `open:` question, not
as a ruling. Rejected: ranking captures above Bartle.
source: director

RULE Evidence is immutable and outranks the code. If they disagree, the code is wrong.
enforced_by: none: judgement

RULE Trust observations, and verify every claim about mechanism before building on it. The corpus
here is written by AI. A model meeting its own earlier output is not meeting a colleague.
enforced_by: none: judgement

## The reference firewall

FACT `G:\Source\mucka` is the previous .NET/MAUI client. Its behaviour has been extracted into
`docs/spec/`, and that is the only form in which it reaches this project.

RULE Never read, search, or port from `G:\Source\mucka`, its install directory, or its GitHub repo.
If the spec is silent, the answer comes from evidence or from the director, not from the
reference. Extraction is finished; there is no exception.
enforced_by: .claude/hooks/reference-firewall.mjs, .claude/settings.json permissions.deny

FACT The hook runs before every tool call. It blocks:
- any path component named `mucka`;
- the bare `G:\Source` directory, and wildcards directly under it;
- the tripwire list;
- any agent write to `.claude/settings*.json` or `.claude/hooks/`.

The director edits the firewall files by hand. A block is the firewall doing its job, not a bug to
work around. Name the sibling directory you need (`MuckaII`, `mud-fe`, `clio-1.8a`) rather than
searching `G:\Source`.

FACT The firewall only applies to sessions started in this worktree. It is configured and its
script logic is tested, but it is not yet proven in a live session. The live test is a director
task: in a fresh session in `G:\Source\MuckaII\main`, confirm that both Bash and Read of the
previous client's CLAUDE.md are blocked, and that a subagent is blocked too.

FACT `scripts/firewall/reference-identifiers.txt` lists distinctive reference type names. Agents
never read it, because it is the reference's vocabulary. `scripts/check-tripwire.py` reads it and
fails if any of those names appears elsewhere in the tree.

## Where things are

FACT `docs/spec/README.md` maps each layer to the specs it answers to. `docs/spec/architecture.md`
pins the layers, threads, event delivery and outbound queue. `docs/build.md` pins the toolchain.
`scripts/check-all.py` runs every repo gate.

## Invariant #0: the command input owns the keyboard

RULE After any tap, click, drag, or toggle, keyboard focus is in the command input. The only
exception is while another real text field is open, and focus returns when it closes. Enforce this
in one place by structure, not with a list of controls.
enforced_by: none: judgement

open: On Android, does a tap or drag on the scroll raise the soft keyboard?
default: Focus stays in the command input, and the soft keyboard is left as it was. A scroll
gesture never raises it.

## Invariant #1: one ordered stream, keyboard to socket

EVIDENCE The director types at 120-130 wpm.

RULE Typed text and Enter form one ordered stream, all the way to the socket. The invariant is
checked at two points:
1. **Frontend to client.** The text handed to the client at each Enter is exactly the content of
   the frontend's line buffer when that Enter was applied. Concatenated, those texts equal the
   typed keystroke stream.
2. **Client to socket.** The client's documented processing happens next: local `$` commands,
   alias and macro expansion, the 7-bit filter. Its output then reaches the socket complete and in
   order. The only additions are probe blocks inserted by the session layer at command boundaries,
   never inside a command, and each one is marked in the wire log. Remove the marked blocks and the
   bytes equal the client's output.

See `docs/spec/session.md` and `docs/spec/architecture.md`.

- The frontend owns the line buffer and applies input events in the order they arrive. Enter acts
  at exactly the place it falls in that order. Never let a widget apply a batch of keystrokes
  before Enter is examined. Nothing may sit between the widget and the read: no binding, no
  dispatcher hop. Wherever a spec says "the box's text", it means this buffer at the moment the key
  is applied. That includes Ctrl+Shift+D and Ctrl+Shift+F. It never means a widget property read
  later.
- Where the platform delivers text as IME commits (Android), take the committed text. Never
  rebuild it from key events.
- Send every outbound line, whether typed or from a macro, through one ordered channel to one
  socket writer.
- Do nothing on the UI thread that could stall typing, even for 50 ms.

enforced_by: none: judgement

## Bytes, not strings

RULE The protocol layer takes and returns bytes. Lossy text decoding is never used there.
enforced_by: none: judgement

RULE What the player types is reduced to printable 7-bit characters at the single send boundary.
Typographic punctuation is transliterated and anything else is dropped. Protocol escape sequences
the client adds (`ESC-[ ... ESC-]`) are added after this step, so they are never filtered. The raw
console sends verbatim and bypasses this step.
enforced_by: none: judgement

## Comments and docs

RULE A comment records what was observed, or a rule the director set. It carries no dates, no
changelog, no theories about the server, and no prose forbidding future work. Git holds the
history.
enforced_by: none: judgement

RULE A spec states requirements in the present tense ("MuckaII sends ..."), whether or not the code
exists yet. A work order (a plan, a migration list, "still to do") is future tense and is deleted
when its work lands. Name symbols, never line numbers. A number travels with the name that owns it.
enforced_by: none: judgement

RULE MUD2 documentation is stored as data tables in `docs/spec/`, not as prose.
enforced_by: none: judgement

RULE The repo's own text files (source, docs, config, scripts) contain only 7-bit characters, and
a glyph the UI needs is written as its code point. This rule is about our files, not about the
wire. The captures (`*.c1`) hold the server's bytes verbatim and are exempt, as are binary assets.
enforced_by: scripts/check-text-files.py (via scripts/check-all.py)

## Tests

RULE Prove a test can fail. Break the code on purpose, confirm the break applied, watch the test
fail, then restore the code.
enforced_by: none: judgement

RULE Say plainly what cannot be tested from here. For UI behaviour, the gate is the director's
hands.
enforced_by: none: judgement

## Git

RULE Commit only on request. Pushing is a separate request.
enforced_by: none: judgement

RULE The repo is bare at `G:\Source\MuckaII`, with worktrees beneath it. A worktree's directory name
equals its branch. Never create a branch unless asked and told which worktree it belongs to.
enforced_by: none: judgement
