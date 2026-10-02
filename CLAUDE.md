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
encoding: printable 7-bit text, with bytes 0x9B-0xFF used as markup. Those bytes form nested code
sequences, each ending `FF FF`, that bracket and label the text, much as tags do in a markup
language. On top of this it sends a small ANSI subset: colour (SGR) and clear-screen, with no
cursor positioning. Input to the server is printable 7-bit characters, plus the client-mode escape
sequences the client itself adds.

RULE Never treat the wire as text. A byte at 0x80 or above is markup to decode, not a character to
transcode, strip, or replace.
enforced_by: none: judgement

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

RULE Evidence is immutable and outranks the code. If they disagree, the code is wrong.
enforced_by: none: judgement

RULE Trust observations, and verify every claim about mechanism before building on it. The corpus
here is written by AI. A model meeting its own earlier output is not meeting a colleague.
enforced_by: none: judgement

## The reference firewall

FACT `G:\Source\mucka` is the previous .NET/MAUI client. Its behaviour has been extracted into
`docs/spec/`, and that is the only form in which it reaches this project.

RULE Never read, search, or port from `G:\Source\mucka`. If the spec is silent, the answer comes
from evidence or from the director, not from the reference. The one exception is an extraction
agent the director has explicitly tasked with writing `docs/spec/`. It writes observable behaviour
only, with no reference type names, file names, or code.
enforced_by: .claude/hooks/reference-firewall.mjs, .claude/settings.json permissions.deny

FACT The firewall protects itself. The hook blocks any agent edit whose text contains the
reference's path, and that includes `.claude/settings.json`, the hook itself, and this section.
The director edits those by hand. A block here is the firewall doing its job, not a bug to work
around.

FACT `scripts/firewall/reference-identifiers.txt` lists distinctive reference type names. Finding
one anywhere else in the tree means the reference leaked in. That list is the only file allowed to
contain them.

## Invariant #0: the command input owns the keyboard

RULE After any tap, click, drag, or toggle, keyboard focus is in the command input. The only
exception is while another real text field is open, and focus returns when it closes. Enforce this
in one place by structure, not with a list of controls.
enforced_by: none: judgement

## Invariant #1: one ordered stream, keyboard to socket

EVIDENCE The director types at 120-130 wpm.

RULE Typed text and Enter form one ordered stream, all the way to the socket. Typed lines reach the
socket unmodified and in order. A probe block the client injects goes only at a command boundary,
never inside a command, and is marked in the wire log. Remove the marked blocks, concatenate the
rest, and the result equals the typed stream. See `docs/spec/session.md`.

- The frontend owns the line buffer and applies input events in the order they arrive. Enter acts
  at exactly the place it falls in that order. Never let a widget apply a batch of keystrokes
  before Enter is examined. Nothing may sit between the widget and the read: no binding, no
  dispatcher hop.
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
the client adds (`ESC-[ ... ESC-]`) are added after this step, so they are never filtered.
enforced_by: none: judgement

## Comments and docs

RULE A comment records what was observed, or a rule the director set. It carries no dates, no
changelog, no theories about the server, and no prose forbidding future work. Git holds the
history.
enforced_by: none: judgement

RULE A doc is in the future tense until its code lands. After that it is in the present tense, and
the work order is deleted. Name symbols, never line numbers. A number travels with the name that
owns it.
enforced_by: none: judgement

RULE MUD2 documentation is stored as data tables in `docs/spec/`, not as prose.
enforced_by: none: judgement

RULE The repo's own text files (source, docs, config, scripts) contain only 7-bit characters, and
a glyph the UI needs is written as its code point. This rule is about our files, not about the
wire. The captures (`*.c1`) hold the server's bytes verbatim and are exempt, as are binary assets.
enforced_by: none: judgement

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
