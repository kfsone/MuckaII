# Open questions

FACT This file gathers every open item in `docs/spec/` (including `later/`) into one deduplicated
table, grouped so the director can rule on them in one pass. An open item is an `open:` line in a
Markdown spec, an `open =` key in `keys.toml` or `sounds.toml`, or a `# open:` comment in
`c1-codes.toml`. Each row names the file and the section, key or code that holds it, and repeats in
short the default that file gives (`default:`, `default =` or `# default:`): what is built until the
director rules. The open items stay where they are; when one is ruled, it is replaced there by a
tagged statement and its row here is deleted. "Who decides" is `director` (a choice MuckaII makes),
`capture` (what the server does) or `bartle-email` (semantics only Bartle can state).
source: director

FACT The "capture settles" marks are an assessment, not a ruling: `yes` means a fresh capture of a
full login plus a short play session would certainly exercise the question, assuming the capture
records `tx` as well as `rx`, starts before the telnet negotiation, and ends with a QUIT back to the
Option menu. A capture made with Mucka shows the server's response to Mucka's choices only, so
"which reply should MuckaII send" stays with the director even when a capture informs it.
source: capture:phone-login-at-20-columns.c1 (the existing captures have no `tx` and no login)

DECISION Evidence questions are settled Bartle first. Bartle's documents may be older than the
newest server code, so a Bartle statement can be deprecated, but only by two or more independent
proofs (captures). The ranking is: Bartle's documents, Bartle's own words, captures, Clio, the
reference. Where Bartle is silent:

| Case | Outcome |
|------|---------|
| a capture speaks | the capture decides |
| no capture, and the remaining sources agree | that agreement decides |
| no capture, and the remaining sources split | the question stays open for the director |

Each settlement is recorded as a DECISION in the owning spec, citing this rule and the deciding
source, and its row here is deleted.
source: director

## A. What the server does (capture)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| A1 | connection.md 1. Transport; persistence.md Wire log (`wave` pair); protocol.md Echo | Outbound line terminator: CR LF, or CR CR LF as in a reference wire-log pair `wave\r\r\n`? | reference wire log vs captures (echo ends `\r\n`) and every spec | capture | yes | `0D 0A` |
| A2 | connection.md 2.1 Replies (NEW-ENVIRON) | Does the server accept USER from NEW-ENVIRON and skip the shell `login:` prompt? Then: WILL or WONT? | Clio (WONT) vs reference (WILL + USER) | capture, then director | yes | WONT (Clio); auto-login answers `login:` |
| A3 | connection.md 2.1 Replies (WILL SGA) | Does the server send WILL SGA more than once? Answer once (Clio) or every time (reference)? | Clio vs reference | capture, then director | yes | once per connection (Clio) |
| A4 | protocol.md Line model | A login prompt split across two reads shows only its second half in the reference. Do real login prompts arrive split? | reference behaviour; no capture | capture | yes | append consecutive partials; match prompts over the byte stream |
| A5 | protocol.md Game mode; flow.md Game | Does the server send any code on a quit to the Option menu? | Clio and reference use text only; no capture of a quit | capture | yes | no code; the text trigger and `95 03` end game mode |
| A6 | protocol.md Prompt | Sessions start with a prompt before any room name, which the reference glues to the next line. Show the entering prompt as a prompt? | captures vs reference line model | capture, then director | yes | yes, show it as a prompt |
| A7 | connection.md 5.1 Probe format | Does the server answer comma-batched `FES,FEW,FEI` in one interrupt, and are FEW/FEI unreliable without FES? | reference vs Bartle (one command per interrupt shown) | capture; reliability: bartle-email | yes | comma-joined block, FES first |
| A8 | sounds.toml clio.070100.wav, clio.070200.wav | Generic bite and sting: `07 01` / `07 02` or with a trailing `00`? | Bartle (one param) vs Clio and reference (two) | capture | no | both forms play the `..00` file |
| A9 | ui.md 4.1 Capture | Is `15 00 01` "dream word used up"? Does the pre-game dreamword text pattern occur? | reference vs Bartle wording; no capture | capture | no | the 4.1 table as written |
| A10 | ui.md 3.3 Field formatting | Does the server floor or round FES field 14 (minutes to reset)? | reference assumes both | capture (timed) | no | floored: N to N+1 minutes away |
| A11 | protocol.md CSI (ESC [) | Which clear-screen form does MUD2 send; is `ESC [ J` / `ESC [ 2 J` honoured? | Clio (honours) vs reference (ignores) | capture | no | honour both (Clio) |
| A12 | session.md Sleep | The exact received text for each way into and out of sleep, mined from the wire corpus | not yet mined | capture (wire corpus) | no | asleep on the player's sleep command or a probe unanswered 5 s; awake on a non-sleep line; drugged not told apart |
| A13 | flow.md Tombstone | The text of each temporary death, and the permadeath code `08 13`, confirmed against the wire corpus | not yet mined | capture (wire corpus) | no | `08 13` = permadeath; Died landmark without it = temporary |

## C. Protocol and telnet choices (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| C1 | connection.md 2.1 Replies (DO SGA) | Reply WONT (Clio) or WILL (reference) to DO SGA? | Clio vs reference | director | no | WONT (Clio) |
| C2 | connection.md 2.3 NAWS size and resend | NAWS: real column count resent on resize (reference), or fixed 80 x 21 (Clio)? Bartle is silent on NAWS and no capture holds an IAC byte, so nothing separates NAWS from `/T`. | Clio vs reference | director (a capture with a resize and its `tx` informs) | no | reference: effective columns x 21, resent on change, matching the `/T` width 3.2 chose |
| C3 | connection.md 3.1 Prompts and replies | Login prompt matching: anchored, one-shot, ordered (Clio) or case-insensitive substrings (reference)? | Clio vs reference | director (capture informs) | no | Clio |
| C5 | connection.md 4.1 On resize | Should a resize at the Option menu also send `/T`? | reference (NAWS only) | director | no | no, NAWS only |
| C7 | protocol.md Parameters | Unlisted code variant: colour by family (reference) or white on black (Clio)? | Clio vs reference | director | no | white on black (Clio, and the RULE above it) |
| C8 | protocol.md CSI (ESC [) | Bold plus 30-37: bright colour (Clio) or separate bold attribute (reference)? | Clio vs reference | director | no | decoder keeps bold and the sent colour; `ui.md` 1.3 draws bright |
| C9 | protocol.md CSI (ESC [) | Accept `ESC [ 0 ; 3 7 ; 4 0 \r \n m` as one SGR reset (Clio) or abandon it at CR (reference)? | Clio vs reference | director | no | accept as one reset (Clio) |
| C10 | c1-codes.toml code 09 04 | Colour of `09 04`-`09 10` act text: light yellow 11 (Clio) or yellow 3 (reference)? Bartle silent, no capture, sources split | Clio vs reference | director | no | yellow 3 (reference; taste) |
| C11 | ui.md 1.3 Attributes | Draw reverse video (`$-R`, SGR 7) as an fg/bg swap? | reference (never drawn); Bartle permits ignoring | director | no | tracked, not drawn |
| C13 | events.md Code events (deferred), `reset_signal` | Report `06 02`, `06 03`, `06 05` for the reset countdown, and `06 04`/`06 06` sent with a trailing 00? | reference (only `06 04`, `06 06`, exactly two numbers) | director | no | reference; schema ids unchanged |
| C14 | events.md Code events (deferred), `spell_phrase` | Give `11 20`/`11 21` (stronger disabling spells) a `spell_phrase`? | reference (none) | director | no | none; schema ids unchanged |

## D. Command box and keys (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| D1 | input.md Invariant #1 and its acceptance condition; keys.toml Enter | Where is the acceptance condition measured, and should line trimming go? | Invariant #1 vs reference trimming and local commands | director | no | the two points of root `CLAUDE.md` Invariant #1; trimming stays |
| D2 | input.md History; keys.toml Up | History size (200 or 40), consecutive-duplicate suppression, wrap or clamp at the ends | reference vs Clio | director | no | 200 entries, duplicates stored, clamp at both ends |
| D3 | input.md History; keys.toml Down | Down with no recall in progress destroys the draft. Keep, no-op, or stash? | reference | director | no | no-op; the draft stays |
| D4 | input.md Scrollback; keys.toml `<any other key>` | Scrollback swallows typed keys, against Invariant #1. Return to live and land the keys in the box instead? | reference vs Invariant #1 | director | no | first printable key returns to live and lands in the line buffer; no key discarded |
| D5 | input.md Scrollback; keys.toml F1..F12, Ctrl+W, Ctrl+R | Do F-keys, Ctrl+R, Ctrl+W and Ctrl+` act in scrollback? keys.toml says Ctrl+R and Ctrl+W return to live first; input.md says the reference swallows them. | keys.toml vs input.md (both reference) | director | no | return to live, then act |
| D6 | input.md Box prefills; ui.md 2.5 Tell lines | Clicking a name: prepend to typed text when the caret is at the start, or insert only into an empty box? | input.md vs ui.md (both reference) | director | no | one rule: prepend when the caret is at the start |
| D7 | input.md Expansion of outgoing text; ui.md 5. Window title; later/session-transcript.md Transcript | Product string: `$VER` text, window title, transcript header/footer and `session-rec.` prefix | reference says "Mucka" | director | no | `MuckaII v<version>`; title `muckaii`; "Recording by MuckaII"; keep `session-rec.` |
| D8 | input.md Positional arguments | Adopt the positional `$1`-`$9` alias arguments the reference defines but rarely reaches? | reference definition vs reference behaviour | director | no | yes: `$name rest` runs `name` with `rest` as arguments |
| D9 | keys.toml Tab | What Tab does inside the box | unspecified | director | no | nothing; focus stays |
| D10 | keys.toml Ctrl+C | Platform copy (reference) or clear line / exit (Clio)? | Clio vs reference | director | no | platform copy |
| D11 | keys.toml Ctrl+Shift+F1..Ctrl+Shift+F12 | Unbound on desktop, Ctrl layer on Android. Pick one. | reference desktop vs reference Android | director | no | unbound on both |
| D12 | keys.toml Ctrl+D | Send `say "<word>"` in game mode only (Clio) or `"<word>"` anywhere (reference)? | Clio vs reference | director | no | `"<word>"`, in game mode only |
| D13 | keys.toml Ctrl+Shift+D, Ctrl+Shift+F | Follow-up text from the box's current text (as specified), not the reference's stale copy. Confirm. | reference defect | director | no | the box's current text |
| D14 | keys.toml Ctrl+Shift+D (android) | Android lacks the chained dreamword form. Intended? | reference desktop vs Android | director | no | keep: same as Ctrl+D |
| D15 | keys.toml note (macOS) | Ctrl or Cmd for the chords on macOS? | none | director | no | Ctrl |

## E. Launch scope (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| E1 | persistence.md Persistence; later/session-transcript.md Recording chip | Session transcript and `rec` chip: launch or later? | INTERNAL.md (wire log only) vs reference | director | no | later |
| E4 | later/chat-mode.md | Chat-only view filter: launch or later? | INTERNAL.md list | director | no | later |
| E5 | later/diagnostic-commands.md | `$SID`, `$MARK`, `$CHATTEST`: launch or later? | wire log (launch) vs diagnostics | director | no | later |
| E8 | ui.md 3.4 Effect icons | Combat status icon: launch or later (depends on combat tracking)? | ui.md vs section 8 | director | no | later; no combat slot at launch |
| E9 | connection.md 1.2 Session-drop classification | Drop headline overlay and automatic relogin after a Reset drop: launch now that persona selection is? | INTERNAL.md list (persona selection and creation only) | director | no | both later |
| E10 | fair-play.md (end) | Watchwords answer triggers in incoming text, so they are conditional. Charter review before they are built | fair-play.md Automation vs the reference feature | director | no | not built; `[watch]` still imported verbatim |
| E11 | input.md `$` commands | What does a `scope: later` `$` command do when typed before it is built? | none | director | no | as `$map`: reserved, falls to watchword speak, nothing sent |

## F. Data, wire log and import (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| F1 | persistence.md Data directory | Confirm the proposed contents of `~/.muckaii/` (settings file, import marker) | proposal | director | no | the proposed contents |
| F2 | persistence.md Data directory | File names inside `~/.muckaii/` | not ruled | director | no | `muckaii.db`, `mucka-ini-imported`, `settings.<ext>` |
| F3 | persistence.md Wire log | Two instances on first run both see no import marker. Which imports? | none | director | no | the one that wins an exclusive-create lock |
| F4 | persistence.md What a record is | Which annotations MuckaII writes | reference list | director | no | the list minus `logttr`, plus masking, probe and raw-console marks |
| F6 | persistence.md Schema forward-compatibility | A database written by a newer MuckaII: read-only, refuse, or carry on? | none | director | no | carry on logging, say so once; never touch unknown steps |
| F7 | persistence.md Finding the file | Both Windows candidates exist: take the first, or ask? | reference order | director | no | take the first silently |
| F8 | persistence.md What is imported | Does declining write the marker? Can the import be re-run from a menu? | none | director | no | declining writes it; cancelling the picker does not; no menu re-run |
| F9 | persistence.md What is imported | Show a summary of what was imported? | none | director | no | yes, one client line of counts |
| F10 | mucka-ini.md mucka.ini | Keys for later features: import and keep (proposed) or drop? | proposal | director | no | import and keep |
| F11 | mucka-ini.md `[profiles]` | Do not invent built-ins from an ini with no profiles (proposed); are MuckaII's first-run built-ins `MUD2 UK` and `MUD2.COM`? | proposal | director | no | no invented profiles; yes, the same two built-ins |
| F12 | mucka-ini.md How the sections combine | One column count per profile, or per-profile plus global as the ini stores? | ini format | director | no | one layered key |
| F13 | mucka-ini.md Obsolete keys | Drop obsolete keys without telling the user (proposed)? | proposal | director | no | drop silently |
| F14 | mucka-ini.md `[settings]` and `[settings:Name]`; sounds.toml `[ini_keys]` | Sound key set: numeric codes and `soundgroupvol.bell` only (mucka-ini.md), or also `soundgroup.bell`, `tell`, `alert`, `tell-wiz`, `alert-flee-failed` (sounds.toml)? | mucka-ini.md vs sounds.toml (both reference) | director | no | accept the union |
| F15 | persistence.md Wire log | With `telnetlogin` off the user types the account ID and password into the box. Mask a line sent while the recognised account or password prompt is the live partial? | masking rule (login state machine sends only) | director | no | yes, mask it the same way |
| F16 | persistence.md Wire log | The account ID also arrives inbound (echo of the account line, the `95` block). Mask it in received records, or keep received bytes verbatim? | masking rule vs "received bytes verbatim" | director | no | keep received records verbatim |
| F17 | persistence.md Settings storage | Importer, profile with `[settings:P]`: a key equal to the global value at import is not written, so a later global edit leaks into P. Write every settings-scope key explicitly? | importer RULE vs Mucka's replace semantics | director | no | differences only |
| F18 | connection.md 6. Command-line arguments | `-password` and `-account` values are visible in process listings and shell history | none | director | no | keep both flags, warn in the help text |
| F19 | persistence.md Retention and deduplication | The retention policy: what ages out, after how long, whether old payloads are compressed | none | director | no | nothing ages out, nothing is compressed |

## G. Look, sounds and assets (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| G1 | ui.md 1.2 Palette | Confirm the Campbell palette | reference | director | no | Campbell |
| G2 | sounds.toml near-far | Play far sounds quieter than near? | Clio and reference (identical) | director | no | identical |
| G3 | sounds.toml theme-at-startup, mucka_theme.wav | Startup theme: play at all; respect the master switch; give it a settings row? | reference | director | no | as the reference: plays once, ungated, no row |
| G4 | sounds.toml mucka.1307.wav, tell.wav, beep.wav, mucka_theme.wav, Perc_Stick_hi.wav | Origin and licence of the non-Clio sound files | undocumented | director | no | ship them as Mucka did |
| G5 | sounds.toml mucka.flee_failed.wav | Fire on the bare "You have fled by trying to go" line whether or not a fight is believed open? | reference (combat tracking) | director | no | fire on the bare line |

## H. Architecture (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles | default |
|----|---------------|----------|---------------------|-------------|-----------------|---------|
| H1 | architecture.md Delivery | Slint's after-rendering notifier fires after rendering, before presentation | none | director (spike step 3 measures) | no | measure to the notifier plus one refresh interval |
| H2 | architecture.md Input: two orderings | `input.md` "Ordering: one queue" described one queue holding box edits | architecture.md vs input.md | director | no | architecture.md governs; `input.md` is now reconciled ("Ordering: two orderings"), so the default is met and only the line in `architecture.md` remains |

FACT 71 items: 13 capture (A1-A13), 58 director choices (C 11, D 15, E 7, F 18, G 5, H 2). The
group B Bartle-semantics rows are ruled and applied in `c1-codes.toml`, so B is empty. A fresh login
plus short play session settles the server side of 7 (A1-A7); A2, A3 and A6 then still need a
director ruling on what MuckaII does. A12 and A13 are settled by mining the wire corpus.
source: director (counted from the open items in `docs/spec/`)
