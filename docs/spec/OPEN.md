# Open questions

FACT This file gathers every `open:` item in `docs/spec/` into one deduplicated table, grouped so the
director can rule on them in one pass. Each row names the file and the section, key or code that
holds the `open:` line. The `open:` lines stay where they are; when one is ruled, it is replaced
there by a tagged statement and its row here is deleted. "Who decides" is `director` (a choice
MuckaII makes), `capture` (what the server does) or `bartle-email` (semantics only Bartle can
state).
source: director

FACT The "capture settles" marks are an assessment, not a ruling: `yes` means a fresh capture of a
full login plus a short play session would certainly exercise the question, assuming the capture
records `tx` as well as `rx`, starts before the telnet negotiation, and ends with a QUIT back to the
Option menu. A capture made with Mucka shows the server's response to Mucka's choices only, so
"which reply should MuckaII send" stays with the director even when a capture informs it.
source: capture:phone-login-at-20-columns.c1 (the existing captures have no `tx` and no login)

## Rulings pending application

DECISION Groups B and C are settled by the evidence ranking in `CLAUDE.md` (Bartle, captures,
Clio, then the reference). The agent that applies a settlement deletes its row here and records the
outcome as a DECISION in the owning spec.
source: director

DECISION E3, E6 and E7 are in launch scope: chat colours, the flee keys, and Ctrl+R reply. So are
Ctrl+D, `^1`-`^3`, and all F-key layers. E2 is later: anti-idle and keep-screen-on. Watchwords are
later as well. Delete those rows once each owning spec's `scope:` lines agree.
source: director

DECISION F5: the outbound password line is masked in the wire log with same-length placeholder
bytes plus an annotation.
source: director

## A. What the server does (capture)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| A1 | connection.md 1. Transport; persistence.md Session transcript; protocol.md Echo | Outbound line terminator: CR LF, or CR CR LF as in a reference wire-log pair `wave\r\r\n`? | reference wire log vs captures (echo ends `\r\n`) and every spec | capture | yes |
| A2 | connection.md 2.1 Replies (NEW-ENVIRON) | Does the server accept USER from NEW-ENVIRON and skip the shell `login:` prompt? Then: WILL or WONT? | Clio (WONT) vs reference (WILL + USER) | capture, then director | yes |
| A3 | connection.md 2.1 Replies (WILL SGA) | Does the server send WILL SGA more than once? Answer once (Clio) or every time (reference)? | Clio vs reference | capture, then director | yes |
| A4 | protocol.md Line model | A login prompt split across two reads shows only its second half in the reference. Do real login prompts arrive split? | reference behaviour; no capture | capture | yes |
| A5 | protocol.md Game mode | Does the server send any code on a quit to the Option menu? | Clio and reference use text only; no capture of a quit | capture | yes |
| A6 | protocol.md Prompt | Sessions start with a prompt before any room name, which the reference glues to the next line. Show the entering prompt as a prompt? | captures vs reference line model | capture, then director | yes |
| A7 | connection.md 5.1 Probe format | Does the server answer comma-batched `FES,FEW,FEI` in one interrupt, and are FEW/FEI unreliable without FES? | reference vs Bartle (one command per interrupt shown) | capture; reliability: bartle-email | yes |
| A8 | sounds.toml clio.070100.wav, clio.070200.wav | Generic bite and sting: `07 01` / `07 02` or with a trailing `00`? | Bartle (one param) vs Clio and reference (two) | capture | no |
| A9 | ui.md 4.1 Capture | Is `15 00 01` "dream word used up"? Does the pre-game dreamword text pattern occur? | reference vs Bartle wording; no capture | capture | no |
| A10 | ui.md 3.3 Field formatting | Does the server floor or round FES field 14 (minutes to reset)? | reference assumes both | capture (timed) | no |
| A11 | protocol.md CSI (ESC [) | Which clear-screen form does MUD2 send; is `ESC [ J` / `ESC [ 2 J` honoured? | Clio (honours) vs reference (ignores) | capture | no |

## B. Code semantics (bartle-email)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| B1 | c1-codes.toml code 00; protocol.md Context stack | Does `00` have its own closing 255, or is it a stack reset? Can a surplus pop remove the base frame it sets? | Bartle (bracketed) vs Clio (reset, base never popped) vs reference (base popped) | bartle-email | no |
| B2 | c1-codes.toml code 98; protocol.md Prompt | Is `98 nn` only a snooped-line prefix, or also a flush / prompt signal? | Bartle vs reference (flush) vs Clio (re-arms prompt gate) | bartle-email | no |
| B3 | c1-codes.toml code 07 02 02; sounds.toml clio.070203.wav | FE4 gives `07 02 02` to both jellyfish and electric eel. Is the eel `07 02 03`? | Bartle FE4 vs Clio | bartle-email | no |
| B4 | ui.md 3.2 Fields and data sources | FES field 15 (weather letter) is on the wire but not in Bartle's FES list. Confirm its meaning and letters. | captures and Clio FES notes vs Bartle | bartle-email | no |
| B5 | connection.md 5.2 Cadence | Does each probe cost the player a game turn? | reference vs Bartle (only says probes do not wake a sleeper) | bartle-email | no |
| B6 | c1-codes.toml code 94 | FE number as two numbers (`oo*100+nn`, FE4) or one (`nn`, FECodes). FE4 governs; confirm. | Bartle FE4 vs FECodes | bartle-email | no |
| B7 | c1-codes.toml code 16 00 00 | Music on 16 (FECodes) or 17 (FE4, which governs); 16 for houses. Confirm. | Bartle FE4 vs FECodes | bartle-email | no |
| B8 | c1-codes.toml code 89 00 00 | FECodes gives stamina as bare `89`; FE4 and the wire give `89 00 00` / `89 00 01`. Close? | FECodes vs FE4 and captures | director (close) | no |
| B9 | c1-codes.toml code 89 00 00 | Clio matches 89 headers without their 255; the captures balance only with it. Close? | Clio vs captures | director (close) | no |

## C. Protocol and telnet choices (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| C1 | connection.md 2.1 Replies (DO SGA) | Reply WONT (Clio) or WILL (reference) to DO SGA? | Clio vs reference | director | no |
| C2 | connection.md 2.3 NAWS size and resend | NAWS: real column count resent on resize (reference), or fixed 80 x 21 (Clio)? What NAWS changes on the server is unconfirmed. | Clio vs reference | director (capture with a resize informs) | no |
| C3 | connection.md 3.1 Prompts and replies | Login prompt matching: anchored, one-shot, ordered (Clio) or case-insensitive substrings (reference)? | Clio vs reference | director (capture informs) | no |
| C4 | connection.md 3.2 Client-mode entry | Keep ESC ^F ESC-N `/T{cols}` inside a command interrupt (reference, capture shows it honoured), or ESC ^F ESC-T bare (Clio)? | Clio vs reference | director | no |
| C5 | connection.md 4.1 On resize | Should a resize at the Option menu also send `/T`? | reference (NAWS only) | director | no |
| C6 | connection.md 5.3 Setup batch on game entry | Which of `identify`, `fightbrief`, `auto fex`, `score` and FEX does MuckaII send at launch? | reference (all, unasked) | director | no |
| C7 | protocol.md Parameters | Unlisted code variant: colour by family (reference) or white on black (Clio)? | Clio vs reference | director | no |
| C8 | protocol.md CSI (ESC [) | Bold plus 30-37: bright colour (Clio) or separate bold attribute (reference)? | Clio vs reference | director | no |
| C9 | protocol.md CSI (ESC [) | Accept `ESC [ 0 ; 3 7 ; 4 0 \r \n m` as one SGR reset (Clio) or abandon it at CR (reference)? | Clio vs reference | director | no |
| C10 | c1-codes.toml code 09 04 | Colour of `09 04`-`09 10` act text: light yellow 11 (Clio) or yellow 3 (reference)? | Clio vs reference | director | no |
| C11 | ui.md 1.3 Attributes | Draw reverse video (`$-R`, SGR 7) as an fg/bg swap? | reference (never drawn); Bartle permits ignoring | director | no |
| C12 | events.md Readings | `room_long` never reports the last row because the server closes `02 02` before the final line end. Keep the reference's behaviour? | capture vs reference reporting | director | no |
| C13 | events.md Code events (deferred), `reset_signal` | Report `06 02`, `06 03`, `06 05` for the reset countdown, and `06 04`/`06 06` sent with a trailing 00? | reference (only `06 04`, `06 06`, exactly two numbers) | director | no |
| C14 | events.md Code events (deferred), `spell_phrase` | Give `11 20`/`11 21` (stronger disabling spells) a `spell_phrase`? | reference (none) | director | no |

## D. Command box and keys (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| D1 | input.md Invariant #1 and its acceptance condition; keys.toml Enter | Where is the acceptance condition measured, and should line trimming go? | Invariant #1 vs reference trimming and local commands | director | no |
| D2 | input.md History; keys.toml Up | History size (200 or 40), consecutive-duplicate suppression, wrap or clamp at the ends | reference vs Clio | director | no |
| D3 | input.md History; keys.toml Down | Down with no recall in progress destroys the draft. Keep, no-op, or stash? | reference | director | no |
| D4 | input.md Scrollback; keys.toml `<any other key>` | Scrollback swallows typed keys, against Invariant #1. Return to live and land the keys in the box instead? | reference vs Invariant #1 | director | no |
| D5 | input.md Scrollback; keys.toml F1..F12, Ctrl+W, Ctrl+R | Do F-keys, Ctrl+R, Ctrl+W and Ctrl+` act in scrollback? keys.toml says Ctrl+R and Ctrl+W return to live first; input.md says the reference swallows them. | keys.toml vs input.md (both reference) | director | no |
| D6 | input.md Box prefills; ui.md 2.5 Tell lines | Clicking a name: prepend to typed text when the caret is at the start, or insert only into an empty box? | input.md vs ui.md (both reference) | director | no |
| D7 | input.md Expansion of outgoing text; ui.md 5. Window title; persistence.md Session transcript | Product string: `$VER` text, window title, transcript header/footer and `session-rec.` prefix | reference says "Mucka" | director | no |
| D8 | input.md Positional arguments | Adopt the positional `$1`-`$9` alias arguments the reference defines but rarely reaches? | reference definition vs reference behaviour | director | no |
| D9 | keys.toml Tab | What Tab does inside the box | unspecified | director | no |
| D10 | keys.toml Ctrl+C | Platform copy (reference) or clear line / exit (Clio)? | Clio vs reference | director | no |
| D11 | keys.toml Ctrl+Shift+F1..Ctrl+Shift+F12 | Unbound on desktop, Ctrl layer on Android. Pick one. | reference desktop vs reference Android | director | no |
| D12 | keys.toml Ctrl+D | Send `say "<word>"` in game mode only (Clio) or `"<word>"` anywhere (reference)? | Clio vs reference | director | no |
| D13 | keys.toml Ctrl+Shift+D, Ctrl+Shift+F | Follow-up text from the box's current text (as specified), not the reference's stale copy. Confirm. | reference defect | director | no |
| D14 | keys.toml Ctrl+Shift+D (android) | Android lacks the chained dreamword form. Intended? | reference desktop vs Android | director | no |
| D15 | keys.toml note (macOS) | Ctrl or Cmd for the chords on macOS? | none | director | no |

## E. Launch scope (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| E1 | persistence.md Persistence; ui.md 3.5 Recording chip | Session transcript and `rec` chip: launch or later? | INTERNAL.md (wire log only) vs reference | director | no |
| E2 | connection.md 5.5 Anti-idle keepalive; mucka-ini.md `[profile:Name]`; ui.md 6. Keep screen on | `antiidle` and `keepscreenon`: launch? | INTERNAL.md list vs ui.md (launch) | director | no |
| E3 | ui.md 2. Chat colouring; mucka-ini.md `[settings]` only | Chat theme, colours and faces: launch (ui.md) or later (mucka-ini.md)? | ui.md vs mucka-ini.md | director | no |
| E4 | input.md Chat mode | Chat-only view filter: launch or later? | INTERNAL.md list | director | no |
| E5 | input.md `$` commands | `$SID`, `$MARK`, `$CHATTEST`: launch or later? | wire log (launch) vs diagnostics | director | no |
| E6 | keys.toml Ctrl+F, Ctrl+Shift+F | Flee keys: launch (safety) or later? | INTERNAL.md list | director | no |
| E7 | keys.toml Ctrl+R | Reply-to-last-tell prefill: launch or later? | INTERNAL.md list | director | no |
| E8 | ui.md 3.4 Effect icons | Combat status icon: launch or later (depends on combat tracking)? | ui.md vs section 8 | director | no |

## F. Data, wire log and import (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| F1 | persistence.md Data directory | Confirm the proposed contents of `~/.muckaii/` (settings file, import marker) | proposal | director | no |
| F2 | persistence.md Data directory | File names inside `~/.muckaii/` | not ruled | director | no |
| F3 | persistence.md Wire log | Two instances on first run both see no import marker. Which imports? | none | director | no |
| F4 | persistence.md What a record is | Which annotations MuckaII writes | reference list | director | no |
| F5 | persistence.md What the user can do with it | Keep the login password verbatim in the wire log, or mask it? | "every byte" rule vs secure storage | director | no |
| F6 | persistence.md Schema forward-compatibility | A database written by a newer MuckaII: read-only, refuse, or carry on? | none | director | no |
| F7 | persistence.md Finding the file | Both Windows candidates exist: take the first, or ask? | reference order | director | no |
| F8 | persistence.md What is imported | Does declining write the marker? Can the import be re-run from a menu? | none | director | no |
| F9 | persistence.md What is imported | Show a summary of what was imported? | none | director | no |
| F10 | mucka-ini.md mucka.ini | Keys for later features: import and keep (proposed) or drop? | proposal | director | no |
| F11 | mucka-ini.md `[profiles]` | Do not invent built-ins from an ini with no profiles (proposed); are MuckaII's first-run built-ins `MUD2 UK` and `MUD2.COM`? | proposal | director | no |
| F12 | mucka-ini.md How the sections combine | One column count per profile, or per-profile plus global as the ini stores? | ini format | director | no |
| F13 | mucka-ini.md Obsolete keys | Drop obsolete keys without telling the user (proposed)? | proposal | director | no |
| F14 | mucka-ini.md `[settings]` and `[settings:Name]`; sounds.toml `[ini_keys]` | Sound key set: numeric codes and `soundgroupvol.bell` only (mucka-ini.md), or also `soundgroup.bell`, `tell`, `alert`, `tell-wiz`, `alert-flee-failed` (sounds.toml)? | mucka-ini.md vs sounds.toml (both reference) | director | no |

## G. Look, sounds and assets (director)

| id | file (anchor) | question | sources in conflict | who decides | capture settles |
|----|---------------|----------|---------------------|-------------|-----------------|
| G1 | ui.md 1.2 Palette | Confirm the Campbell palette | reference | director | no |
| G2 | sounds.toml near-far | Play far sounds quieter than near? | Clio and reference (identical) | director | no |
| G3 | sounds.toml theme-at-startup, mucka_theme.wav | Startup theme: play at all; respect the master switch; give it a settings row? | reference | director | no |
| G4 | sounds.toml mucka.1307.wav, tell.wav, beep.wav, mucka_theme.wav, Perc_Stick_hi.wav | Origin and licence of the non-Clio sound files | undocumented | director | no |
| G5 | sounds.toml mucka.flee_failed.wav | Fire on the bare "You have fled by trying to go" line whether or not a fight is believed open? | reference (combat tracking) | director | no |

FACT 76 items: 11 capture, 9 Bartle semantics (2 of them closable by the director), 56 director
choices. A fresh login plus short play session settles the server side of 7 (A1-A7); A2, A3 and A6
then still need a director ruling on what MuckaII does.
source: capture:phone-login-at-20-columns.c1 (assessment against the existing captures)
