# Protocol: the inbound byte stream

FACT This file says how MuckaII turns the bytes MUD2 sends into styled text, lines, a prompt and
structured readings. `c1-codes.toml` lists every C1 code. `events.md` defines the event stream a
decoder emits, which is how every statement here is tested against the captures. Telnet option
negotiation, login and client-mode entry belong to `connection.md`.
source: director

## Layers

FACT The stream is decoded in this order. Each layer hands the next one bytes, never decoded
strings.
source: director

| Order | Layer | Consumes | Produces |
|-------|-------|----------|----------|
| 1 | Telnet | IAC sequences, IAC IAC, CR NUL | Data bytes |
| 2 | MUD2 stream | C1 codes, lone 255, ESC - escapes, CSI, BEL, CR, LF, text | Styled text runs, line ends, stack events, readings |
| 3 | Line model | Text runs, line ends, prompts, clears | Completed lines and one live partial line |
| 4 | Render | Lines | Rows hard-wrapped at the column count |

source: director

## Telnet residue in the stream

FACT A data byte 255 is sent on the wire as `FF FF` (telnet IAC IAC). Every 255 in Bartle's code
documentation therefore appears as `FF FF` in a capture, and every `FF FF` in the captures is one
255 after telnet decoding.
source: bartle:FE4 Codes and their meanings
source: clio:src/telnet.l

EVIDENCE The `.c1` captures hold raw wire bytes: every code header ends `FF FF`, every close is
`FF FF`, and every game line ends `0D 00 0D 0A` (CR NUL CR LF, the telnet form of a bare CR).
source: capture:blind-vs-water-snake.c1

RULE Statements below about "255" mean the telnet-decoded byte. Statements quoting wire bytes say
"on the wire".
enforced_by: none: judgement
source: director

FACT An `FF` on the wire followed by anything other than `FF` is a telnet command. The reference
accepts telnet commands between C1 codes and text at any point.
source: reference
scope: launch

FACT If an `FF` followed by a non-`FF` byte occurs inside a C1 header, the reference treats both
bytes as more header and keeps reading.
source: reference
scope: launch

## C1 framing

FACT A C1 code is a header of 1 to 4 numbers, each 0 to 99, sent as one byte per number with 155
added (lead byte `0x9B + n`), and terminated by 255. On the wire: `<bytes> FF FF`.
source: bartle:FE4 section 3
scope: launch

FACT A lone 255 that is not the end of a header closes the most recent open code. On the wire: a
bare `FF FF`.
source: bartle:FE4 section 3
scope: launch

FACT Codes nest. When an inner code closes, the enclosing code applies again. Every code except the
catch and throw codes has a matching close, and some brackets hold no text at all.
source: bartle:FE4 section 4
scope: launch

EVIDENCE Nesting seen on the wire, written `<code>` for a header and `</>` for a lone 255:

| Shape | Capture |
|-------|---------|
| `<20 03><02 01>Underground confluence</>.\r\0\r\n</>` | capture:blind-vs-water-snake.c1 |
| `<12 08 01><89 00 00><99 10>120</></> <89 00 01><99 10>120</></> 97 100 102 100 80 120 51689 N N N N 4 T\r\0\r\n</>` | capture:blind-vs-water-snake.c1 |
| `<03 01 02>A small, stoppered vial stands here, labelled: "<12 06>POTION 1</>". \r\0\r\n</>` | capture:invisible-man-preemptive.c1 |
| `<12 08 05><12 03><05 00 06>Kayfez the scout</>\r\0\r\n</></>` | capture:dark-two-rats.c1 |

### Parameters

FACT A missing trailing number reads as 00. The codes are hierarchical, so `08` and `08 00` are the
same code, as are `11` and `11 00`, and `07 02` and `07 02 00`.
source: bartle:FECodes handleCode
scope: launch

EVIDENCE The wire sends both forms. `08 00`, `11 00` and `07 02 00` appear with the trailing zero;
`01` and `12` appear as a single byte.
source: capture:invisible-player-joins.c1
source: capture:wyvern-poison-death.c1

RULE The decoder matches a header to an id in `c1-codes.toml` by the first of these that succeeds,
and dispatches on that id. A header that matches no id keeps its raw numbers in the event and pushes
white on black.

| Step | Match |
|------|-------|
| 1 | The header's numbers equal an id exactly |
| 2 | Its first number is that of a parametric entry (non-empty `params`: 94, 96, 97, 98, 99): that entry, with the remaining numbers as its params (`99 99` matched at step 1 first) |
| 3 | Trailing zeros removed one at a time until the numbers equal an id (`08 00` is `08`) |
| 4 | Zeros appended, up to three numbers, until they equal an id (bare `20` is `20 00`) |

enforced_by: none: judgement
source: clio:src/telnet.l
scope: launch

open: The reference colours an unlisted variant by its family (any `04 ...` is magenta, any `03 01 ...`
cyan) where Clio's catch-all pushes white on black.
default: white on black, as Clio does and as the RULE above states.

FACT Fall-through between cases in the older document's sample code is not semantics; only its
treatment of missing parameters is.
source: director

## Context stack

FACT The stack holds one frame per open code. The text style is the colour of the top frame. With
no frame open, text has the terminal's default style.
source: bartle:FE4 section 3
source: reference
scope: launch

| Operation | Trigger | Effect | Source |
|-----------|---------|--------|--------|
| push | Any header whose `stack` is `push` | New frame with the code's colour on top | bartle:FE4 section 4 |
| push_empty | `96` | As push; the server closes it at once | bartle:FE4 Codes and their meanings |
| pop | Lone 255 | Remove the top frame; style reverts to the frame below | bartle:FE4 section 3 |
| pop on empty | Lone 255 with nothing open | Ignored; style stays default | clio:src/wstack.c |
| catch | `90`, `90 00` | Save the whole stack, begin an empty one whose base colour is the current colour; no frame, no colour change | bartle:FE4 section 4 |
| throw | `90 01`, and any other `90 xx` | Discard the current stack and reinstate the saved one; no frame | bartle:FE4 section 4 |
| throw with no catch | `90 01` with nothing saved | Ignored | clio:src/wstack.c |
| pop below catch | Lone 255 when the stack begun by the catch is empty | Ignored; cannot reach frames saved by the catch | clio:src/wstack.c |
| push (`00`) | `00` | As push, with white on black; its own lone 255 pops it. No reset of the stack | bartle:FE4 section 4 |

FACT Catches nest: each throw reinstates the stack saved by the most recent unmatched catch.
source: clio:src/wstack.c
scope: launch

DECISION `00` (Initialise) is an ordinary bracketed code: it pushes a frame and has its own closing
255, like every code except catch and throw. It does not reset the stack, so there is no base frame
for a surplus pop to remove. Bartle states that only catch and throw lack a matching 255, and no
capture holds a `00`, so nothing deprecates that. Rejected: Clio's and the reference's stack reset
(Clio: the base frame is never popped; reference: it can be popped). `c1-codes.toml` code `00`
records it as `stack = "push"`.
source: director (evidence ranking: Bartle first; deprecating Bartle needs two or more independent
captures); bartle:FE4 section 4

EVIDENCE A frame can span a line end. `12` brackets "The water-snake1 looks fit.\r\0\r\n" and closes
after the line end; `02 02` brackets a long description the server wrapped over four lines.
source: capture:blind-vs-water-snake.c1
source: capture:dark-two-rats.c1

EVIDENCE The server sends surplus closes. After `<07 02 00>The wyvern stings you with its venomous
tail.</>`, the next line `Stamina=<89 00 00><99 02>84</></>/<89 00 01><99 10>99</></>.</>` ends with a
lone 255 that has nothing open to close.
source: capture:wyvern-poison-death.c1

RULE A surplus close is not an error and changes nothing.
enforced_by: none: judgement
source: capture:wyvern-poison-death.c1
scope: launch

### Colour indices

FACT Colours are indices 0-15 in the order code 99 uses. Backgrounds use 0-7.
source: bartle:FE4 Codes and their meanings

| Index | Name | Index | Name |
|-------|------|-------|------|
| 0 | black | 8 | grey |
| 1 | red | 9 | light red |
| 2 | green | 10 | light green |
| 3 | brown | 11 | yellow |
| 4 | blue | 12 | light blue |
| 5 | magenta | 13 | light magenta |
| 6 | cyan | 14 | light cyan |
| 7 | light grey | 15 | white |

FACT `99 fg bg` pushes foreground `fg` on background `bg`; a missing number is 0, so bare `99` is
black on black. `99 99` is the default colour, white on black.
source: bartle:FE4 Codes and their meanings
scope: launch

FACT Every other code pushes the fixed colour in its `fg` and `bg` columns in `c1-codes.toml`.
source: clio:src/telnet.l
source: reference
scope: launch

## Game mode

FACT Game mode is the client's own state: logged in to a persona and playing. Bartle defines no
code for it. Its edges are inferred from codes and text.
source: clio:src/telnet.l
source: reference

| Edge | Trigger | Source |
|------|---------|--------|
| enter | Header `01 01`, `01 02` or `01 03` (the prompt core) | clio:src/telnet.l |
| enter | Header `02 01` (a room name) | clio:src/telnet.l |
| exit | Header `95 03` (account logout) | reference |
| exit | The text `Option (H for help)` completed starting at column 0 while in game mode | clio:src/telnet.l |
| exit | Connection closed | reference |

FACT Neither Clio nor the reference recognises a code for a quit to the option menu; both detect it
from the menu prompt text, hence the text trigger.
source: clio:src/telnet.l
source: reference
scope: launch

open: Does the server send any code when a player quits to the option menu? No capture covers a
quit.
default: no code is expected. Game mode ends on the exit triggers in the table above (the menu
prompt text at column 0, `95 03`, or the connection closing).

FACT Clio also accepts `Option: ` at the start of a line as the menu prompt.
source: clio:src/telnet.l
scope: launch

FACT Leaving game mode clears the context stack and every saved stack without running any
end-of-bracket action, and discards a half-read prompt.
source: reference
scope: launch

## Prompt

FACT The prompt is everything inside the outer `01` bracket, not just the `*`. Code `01 0n` brackets
the core prompt characters; the brackets marking invisibility sit between the two.
source: bartle:FE4 Codes and their meanings
scope: launch

EVIDENCE The two prompt shapes on the wire:

| State | Shape | Capture |
|-------|-------|---------|
| visible | `<01><01 02>*</></>` | capture:dark-two-rats.c1 |
| invisible | `<01>(<01 02>*</>)</>` | capture:invisible-player-joins.c1 |

EVIDENCE The prompt changes from `(*)` to `*` on the line after "You have suddenly and magically
regained your visibleness!".
source: capture:invisible-player-joins.c1

FACT Clio's prompt pattern also allows, before the core, `01 04` bracketing `&` (snooping) and
`01 05` bracketing `>`, and any number of `(` or `[` before and `)` or `]` after.
source: clio:src/telnet.l
scope: launch

RULE Never assume the prompt is `*`. The prompt is whatever the outer `01` bracket holds.
enforced_by: none: judgement
source: director
scope: launch

EVIDENCE No prompt is followed by a line end. The next output starts on the prompt's line: the
echo of the typed command, or game text such as `I don't know to what "man" you're referring.`
straight after the prompt.
source: capture:invisible-man-preemptive.c1
source: capture:phone-login-at-20-columns.c1

EVIDENCE The server sends a prompt after every reply to a front-end probe (FES, FEI, FEX, FEW), so
several prompts arrive with no game output between them.
source: capture:dark-two-rats.c1

FACT The prompt gate: a prompt is shown only if a visible line has been completed since the last
prompt was shown. Otherwise the whole bracket is discarded. Lines inside an FE reply bracket, and
the FES line, do not count as visible.
source: clio:src/telnet.l
source: reference
scope: launch

FACT The prompt is shown as the live partial line (see "Line model") when its outer bracket closes.
source: reference
scope: launch

FACT The reference captures a prompt bracket only if it was already in game mode when the outer `01`
arrived. The prompt that enters game mode is decoded as ordinary text, is not flushed (game mode is
now on), and joins the front of the next completed line.
source: reference
scope: launch

open: The captures `invisible-player-joins.c1`, `wyvern-poison-death.c1` and
`phone-login-at-20-columns.c1` begin mid-session with a prompt before any room name, so a decoder
started outside game mode renders `(*)` or `*` glued to the next line. `events.md` primes the
decoder into game mode for captures to keep it out of golden diffs. Should the entering prompt be
shown as a prompt instead?
default: yes. An outer `01` bracket whose core header enters game mode is captured and shown as a
prompt, because the prompt is the bracket, never a guess at its text (root `CLAUDE.md`). Priming
keeps the golden diffs unchanged.

FACT If a line end arrives while the outer `01` bracket is still open, the reference abandons the
prompt capture and renders what it held as ordinary text on that line.
source: reference
scope: launch

FACT When the outer `01` arrives while an earlier `01` is still open, the new bracket owns the
prompt and the earlier one's capture is restarted.
source: reference
scope: launch

DECISION `98 nn` is only the prefix of a line snooped through snoop `nn`, as Bartle defines it. It
does not flush pending text as a partial line and does not re-arm the prompt gate. No capture holds
a `98`, so nothing deprecates Bartle's definition. Rejected: the reference's flush on `98`, and
Clio's prompt-gate re-arm. `c1-codes.toml` code `98` records the same ruling.
source: director (evidence ranking: Bartle first; deprecating Bartle needs two or more independent
captures); bartle:FE4 code 98

## Echo

EVIDENCE The typed command comes back from the server ending `\r\n`, with no NUL, while game lines end
`\r\0\r\n`. It arrives after the prompt it answers, followed in the same read by the reply.
source: capture:blind-vs-water-snake.c1

EVIDENCE Commands the client sent itself are echoed the same way, several in one read:
`identify\r\nfightbrief\r\nauto fex\r\nscore\r\n`.
source: capture:phone-login-at-20-columns.c1

FACT The echo is ordinary text to the decoder. The line model merges it onto the prompt's line.
source: reference
scope: launch

open: `persistence.md` reports a reference wire-log pair where `wave` was sent and echoed as
`wave\r\r\n`, while the captures show the echo ending `\r\n` and every spec says outbound lines end
CR LF. Which terminator does the server receive and echo?
default: send `0D 0A`, as Clio does, the captures' echo shows and every spec states.

## Line model

FACT A line end is LF (0x0A). CR (0x0D) and NUL (0x00) are dropped, so `\r\0\r\n` and `\r\n` both end
exactly one line.
source: clio:src/telnet.l
source: reference
scope: launch

FACT The screen holds completed lines and at most one live partial line. The partial is a prompt, or
pre-game text without a line end.
source: reference
scope: launch

| Input | Effect | Source |
|-------|--------|--------|
| partial line | Replaces the live partial wholesale | reference |
| empty completed line, partial present | The partial is committed as a line; no blank line is added | reference |
| empty completed line, no partial | A blank line is committed | reference |
| non-empty completed line, partial present | The partial and the line are committed as one line (prompt then echo) | reference |
| non-empty completed line, no partial | The line is committed | reference |
| clear screen | Text before it on the line is committed (merged into a partial as above), a partial alone is committed, then a clear marker is committed and the live screen starts after it | reference |

FACT A clear screen does not delete scrollback. The live screen shows only what follows the clear;
scrollback keeps the lines above it and shows a horizontal rule where the clear happened. No rule is
committed with nothing above it or directly after another rule.
source: director
source: reference
scope: launch

FACT Before game mode, the reference emits whatever text has accumulated as a partial line at the end
of every network read, so login prompts such as `Account ID:` appear without a line end. In game
mode it does not; only the prompt bracket produces a partial.
source: reference
scope: launch

open: Each pre-game flush starts the next partial empty, and a partial replaces the previous one, so a
login prompt split across two reads would show only its second half. No capture covers it.
default: the line model appends a `partial` to the live partial when no line end came between them,
so a split prompt shows whole. Prompt recognition matches over the byte stream, as Clio does
(`connection.md` 3.1).

FACT In game mode, a completed line consisting only of `*` characters is not displayed.
source: reference
scope: launch

FACT Before game mode, a completed line `[New terminal width is N]` is not displayed and confirms the
width N.
source: reference
scope: launch

### Hard wrap

EVIDENCE The server wraps its own output at the width it was told, including inside codes. At 20
columns the FES line arrives as three lines, `99 99 100`, `100 0 100 5902 N N`, `N N 19 F`; at about
100 columns a long description wraps mid-sentence.
source: capture:phone-login-at-20-columns.c1
source: capture:dark-two-rats.c1

FACT The client also hard-wraps each logical line at render time to the column count it told the
server: no word awareness, a row break every `columns` characters, styles kept across the break. When
the server has already wrapped, this changes nothing.
source: reference
scope: launch

| Value | Columns | Source |
|-------|---------|--------|
| fewest told to the server | 20 | reference |
| most told to the server | 160 | reference |
| told when no width can be measured | 40 | director |

FACT A tab expands to spaces up to the next multiple of 8 columns, counted across style runs, before
wrapping.
source: reference
scope: launch

FACT A soft wrap is a render artifact: copying text joins the rows of one logical line.
source: reference
scope: launch

## Control characters

| Byte | Handling | Source |
|------|----------|--------|
| 0x07 BEL | Bell event; no text | clio:src/telnet.l |
| 0x00 NUL | Dropped | clio:src/telnet.l |
| 0x09 TAB | Kept; expanded at render | reference |
| 0x0A LF | Line end | clio:src/telnet.l |
| 0x0C FF | Clear screen when produced by ESC-C; see "Line model" | reference |
| 0x0D CR | Dropped | clio:src/telnet.l |
| 0x1B ESC | Starts an escape; see below | bartle:FE4 section 2 |
| other 0x01-0x1F, 0x7F | Stripped before rendering | reference |
| 0x80-0x9A | Rendered as text by the reference | reference |
| 0x9B-0xFE | C1 header byte | bartle:FE4 section 3 |

FACT MUD2 sends 7-bit text, so bytes 0x80-0x9A should not occur.
source: director

EVIDENCE The six captures contain no ESC, no TAB, no BEL and no byte 0x80-0x9A; the only control
bytes are CR, NUL and LF.
source: capture:blind-vs-water-snake.c1
source: capture:dark-two-rats.c1
source: capture:invisible-man-preemptive.c1
source: capture:invisible-player-joins.c1
source: capture:phone-login-at-20-columns.c1
source: capture:wyvern-poison-death.c1

## MUD2 escapes (ESC -)

FACT Bartle writes these as `$-x`, where `$` is ESC (0x1B) and `-` is 0x2D. The server sends:
source: bartle:FE4 section 2

| Sequence | Meaning | Client behaviour | Source |
|----------|---------|------------------|--------|
| `ESC - C` | Clear screen | Clear (see "Line model") | bartle:FE4 section 2 |
| `ESC - R` | Reverse video on | Set reverse on the current style | bartle:FE4 section 2 |
| `ESC - r` | Reverse video off | Clear reverse | bartle:FE4 section 2 |
| `ESC - K` | Erase to end of line | Event only; the write position is always the line end, so nothing is erased | reference |
| `ESC - <digits> W` | The width is now `<digits>` | Width confirmed; the following `[New terminal width is N]` text up to LF is swallowed | reference |
| `ESC - Q` | The session is over (sent only under MUDPLEX) | Event only; the connection closing ends the session | bartle:FE4 section 2; reference |
| `ESC - <other>` | Not defined | Consumed silently | reference |

FACT Bartle says `ESC-C` and `ESC-K` at least should be handled, and that the server generates no
other escapes.
source: bartle:FE4 section 2
scope: launch

FACT If a byte 0x80 or above arrives while the width notice text is being swallowed, the swallowing
stops and the byte is decoded normally.
source: reference
scope: launch

FACT The escapes the server accepts from the client (`ESC - [` ... `ESC - ]` command interrupt,
`ESC - <n> I`, `ESC - G`, `ESC - T`, `ESC - N`, and `ESC ^F` for client mode) are outbound and belong to
`connection.md`.
source: bartle:FE4 section 2
source: bartle:FE4 section 1

## CSI (ESC [)

FACT MUD2 sends only colour (SGR) and clear screen from ANSI; it never positions the cursor.
source: director

| Final | Parameters | Reference | Clio |
|-------|-----------|-----------|------|
| `m` | 0 or empty: reset | yes | yes |
| `m` | 1 bold, 3 italic, 4 underline, 5 blink, 7 reverse | yes | 1 and 7 only |
| `m` | 22 23 24 25 27: turn the above off | yes | no |
| `m` | 30-37 foreground, 40-47 background | yes | yes |
| `m` | 39 default foreground, 49 default background | yes | no |
| `m` | 90-97 bright foreground, 100-107 bright background | yes | no |
| `J`, `2J` | Clear screen | ignored | yes |
| `K` | Erase to end of line | ignored | yes |
| `H`, `f`, `r;cH` | Cursor position | ignored | yes |

source: reference
source: clio:src/telnet.l
scope: launch

FACT In the reference, an empty SGR parameter between semicolons reads as 0, and a CSI with any final
byte other than `m` is discarded whole.
source: reference
scope: launch

open: Clio turns bold plus 30-37 into the bright colours 8-15; the reference keeps bold as a separate
attribute and leaves the colour as sent.
default: the decoder keeps bold as an attribute and the colour as sent, so the event records what
the wire said. `ui.md` 1.3 draws bold with bright promotion, so the screen shows Clio's colours.

open: Clio honours `ESC [ J` and `ESC [ 2 J` as clear screen; the reference ignores them. MUD2 is said
to send clear screen, and no capture shows which form.
default: honour both as clear screen, as Clio does. Root `CLAUDE.md` says the server sends ANSI
clear-screen.

open: Clio accepts `ESC [ 0 ; 3 7 ; 4 0 \r \n m` as one SGR reset, calling it a server bug
workaround. The reference abandons the CSI at the CR, so the LF ends a line and `m` prints as text.
default: accept it as one SGR reset, as Clio does. Clio outranks the reference on mechanics.

## Encoding

FACT Inbound text is bytes 0x20-0x7E. The decoder never decodes inbound bytes lossily.
source: director
scope: launch

RULE What the player types is reduced to printable 7-bit characters at the single send boundary.
Typographic punctuation is transliterated, and anything else is dropped. The client's own escape
sequences (`ESC-[ ... ESC-]`) are added after that step and are never filtered. Each line ends
CR LF.
enforced_by: none: judgement
source: director
scope: launch

DECISION Player-typed text is masked to 7 bits. Rejected: the reference's behaviour of encoding outbound
text as Latin-1 and sending bytes 0x80-0xFF unmasked, which MUD2 does not accept and which would need
IAC doubling for 0xFF.
source: director
source: reference

## Reading codes

### Stat brackets (89)

FACT `89 00 00`, `89 00 01` and `89 01` bracket the player's own stamina, maximum stamina and score.
The number is displayed as usual and is also a reading. A reading taken from a code outranks the same
number found in prose, which a player's speech can imitate.
source: bartle:FE4 Codes and their meanings
source: reference
scope: launch

EVIDENCE Each 89 bracket holds a 99 colour frame and then the digits:
`<89 00 00><99 02>116</></>`. The places observed:

| Context | Example | Capture |
|---------|---------|---------|
| Being hit in a fight (`08 03`) | `Something hits you (116/120).` | capture:blind-vs-water-snake.c1 |
| After an isolated hit (`07 02`) | `Stamina=84/99.` | capture:wyvern-poison-death.c1 |
| Score save after a kill or a flee | `(Persona saved on +22 = 2,490).` with `89 01` | capture:dark-two-rats.c1 |
| Inside the SCORE reply (`12 02`) | `stamina:` ... `max:` | capture:phone-login-at-20-columns.c1 |
| Inside the FES line | first two fields | capture:blind-vs-water-snake.c1 |

FACT A reading is the bracket's text with commas removed. If anything other than digits remains, the
bracket gives no reading. A bracket nested inside an open 89 bracket does not start a new reading.
source: reference
scope: launch

FACT Readings are reported at the line end of the line that carried them.
source: reference
scope: launch

### FES line (12 08 01)

FACT The reply to `FES` is one line of space-separated fields inside a `12 08 01` bracket. It is not
displayed.
source: bartle:FE4 FES (FE SCORE)
source: reference
scope: launch

| # | Field | Form | Source |
|---|-------|------|--------|
| 1 | stamina (as it appears) | integer, inside `89 00 00` | bartle:FE4 FES (FE SCORE) |
| 2 | maximum stamina | integer, inside `89 00 01` | bartle:FE4 FES (FE SCORE) |
| 3 | strength (effective) | integer | bartle:FE4 FES (FE SCORE) |
| 4 | maximum strength | integer | bartle:FE4 FES (FE SCORE) |
| 5 | dexterity (effective) | integer | bartle:FE4 FES (FE SCORE) |
| 6 | maximum dexterity | integer | bartle:FE4 FES (FE SCORE) |
| 7 | magic (as it appears) | integer | bartle:FE4 FES (FE SCORE) |
| 8 | maximum magic | integer | bartle:FE4 FES (FE SCORE) |
| 9 | points (score) | integer | bartle:FE4 FES (FE SCORE) |
| 10 | blinded | `Y` or `N` | bartle:FE4 FES (FE SCORE) |
| 11 | deafened | `Y` or `N` | bartle:FE4 FES (FE SCORE) |
| 12 | crippled | `Y` or `N` | bartle:FE4 FES (FE SCORE) |
| 13 | dumbed | `Y` or `N` | bartle:FE4 FES (FE SCORE) |
| 14 | minutes to the next reset | integer | bartle:FE4 FES (FE SCORE) |
| 15 | weather | one letter | capture:blind-vs-water-snake.c1 |

EVIDENCE Every FES line in the captures has 15 fields; the 15th is `T`, `R` or `F`. The score field is
zero-padded to at least four digits (`0752`, `0702`, `0655`) while the same score in prose is `702`.
source: capture:invisible-man-preemptive.c1
source: capture:dark-two-rats.c1
source: capture:invisible-player-joins.c1

FACT The weather letters are `F` fair, `C` cloudy, `R` raining, `S` snowing, `O` overcast, `T` heavy
rain or thunder, `B` blizzard.
source: clio:FES.md
scope: launch

FACT The stamina colour is the 99 colour inside the stamina bracket.
source: clio:src/telnet.l
scope: launch

FACT The line ends at the first line end once 15 fields have been read. A line end before 14 fields is
a server wrap and reading continues. After exactly 14 fields, printable text next means the weather
was wrapped onto the next line; anything else means the line had no weather field. The rest of the
line ending (CR, NUL, LF) is absorbed.
source: reference
scope: launch

FACT A FES line with fewer than 14 fields gives no reading.
source: reference
scope: launch

### Room

FACT `02 01` brackets the room name, `02 02` the long description.
source: bartle:FE4 Codes and their meanings
scope: launch

EVIDENCE Every room name on the wire sits inside an ambient-sound bracket (`20 00` or `20 03`) at the
start of a line, with the full stop after the name outside the `02 01` bracket. Objects (`03`) and
creatures (`04`) follow the long description on the same lines.
source: capture:dark-two-rats.c1
source: capture:blind-vs-water-snake.c1

FACT A room name counts as entering or looking at a room only when its `02 01` header arrives before
any printable text on the line. C1 codes do not advance the column. A second `02 01` on the same line
does not count again.
source: clio:src/telnet.l
source: reference
scope: launch

EVIDENCE `It's too dark to see now.` arrives uncoded, followed by an empty FEX reply.
source: capture:dark-two-rats.c1

### Object and creature lists

FACT Codes `03`, `04` and `05` bracket things in a list or in the room; the third number says how they
appear (here, arriving, departing, visible, invisible, on the WHO list). See `c1-codes.toml`.
source: bartle:FE4 Codes and their meanings
scope: launch

EVIDENCE `04 0x 0y` brackets a whole sentence ("A slimy, slithering water-snake surfaces in front of
you. "), not a name, and can start mid-line and wrap.
source: capture:blind-vs-water-snake.c1
source: capture:dark-two-rats.c1

FACT An object named inside a creature sentence is nested in its own `03` bracket.
source: bartle:FE4 section 3

### FE replies

FACT The FEI reply lists the portable objects in the room, one per line, then a line `========`, then
the objects carried. The room part is `--` when the player is blind and `oo` when the room is dark.
Names carry ID numbers whatever the player's setting.
source: bartle:FE4 FEI (FE INVENTORY)
scope: later

EVIDENCE All three room forms appear: names (`vial0`, `vial1`), `--` while blind, `oo` in the dark.
An empty room gives `========` as the first line.
source: capture:invisible-man-preemptive.c1
source: capture:blind-vs-water-snake.c1
source: capture:dark-two-rats.c1

FACT The FEX reply is the apparent exits on one line, separated by spaces. AUTO FEX makes the server
send it after every move.
source: bartle:FE4 FEX (FE EXITS)
scope: later

EVIDENCE In the dark the FEX reply is an empty line.
source: capture:dark-two-rats.c1

FACT The FEW reply is the WHO output: `12 08 05` holding `12 03`, holding one WHO-name bracket per
player with the name and rank (`<name> the <rank>`).
source: capture:dark-two-rats.c1
scope: later

FACT FE reply text is not displayed, and its line ends do not count as visible lines for the prompt
gate.
source: reference
scope: launch

FACT No FE command produces output while the player is asleep.
source: bartle:FE4 FEX (FE EXITS)

### Speech (09)

FACT `09` is the speaker of a message. The reference expects an outer `09` (or `09 00`) frame holding
the whole message, with the quoted words in an inner `09 01` shout, `09 02` say or `09 03` tell
frame, and an act or emote in an inner `09 04` to `09 10` frame.
source: bartle:FE4 Codes and their meanings
source: reference
scope: launch

EVIDENCE No capture contains a 09 code.
source: capture:blind-vs-water-snake.c1

### Line kinds

FACT Each completed line has one kind, derived from codes alone, never from its words.
source: reference
scope: launch

| Kind | Derived from | Source |
|------|--------------|--------|
| `chat` | A `09` header on the line, or any of the line's text emitted inside a `09` frame, or a `09` frame still open at the line end | reference |
| `fight_start` | A `08` (`08 00`) header on the line | bartle:FE4 Codes and their meanings |
| `fight_end` | A `08 10`, `08 11` or `08 12` header on the line | bartle:FE4 Codes and their meanings |
| `creature_invisible` | A `04 00 05` or `04 01 05` header on the line | bartle:FE4 Codes and their meanings |
| `normal` | Anything else | reference |

FACT When more than one applies, `chat` wins; otherwise the last code seen on the line decides.
source: reference
scope: launch

FACT A `chat` line whose `09` frame was already open when the line began continues the previous chat
line (the server wrapped one message).
source: reference
scope: launch

### Fight ends and kills

FACT `08 10`, `08 11` and `08 12` state that a fight ended and why: withdraw, flee, other. They never
say which opponent.
source: bartle:FE4 Codes and their meanings
scope: launch

EVIDENCE `08 11` brackets `You have fled by going north.`; `08 12` brackets `You can fight the wyvern
no longer.` after the uncoded line `The wyvern drops dead, poisoned...`; `08 08` brackets `You have
killed the rat18.`, followed by an uncoded score-save line carrying `89 01`.
source: capture:invisible-man-preemptive.c1
source: capture:wyvern-poison-death.c1
source: capture:dark-two-rats.c1

EVIDENCE A creature dying of anything but the player's blow carries no code: `The zombie5 has
expired.`, `The wyvern has just passed on.`.
source: capture:invisible-player-joins.c1
source: capture:wyvern-poison-death.c1

### Persona wipe (08 13)

FACT `08 13` says the persona is not being saved. The reference zeroes the stamina, score, strength,
dexterity and magic readings and reports the wipe.
source: bartle:FE4 Codes and their meanings
source: reference
scope: launch

### Client-mode data (95)

FACT Entering client mode (`/Mn` or `ESC ^F`) produces its reply inside a `95` bracket: five lines,
each ending in a line end.
source: bartle:FE4 section 1

| Line | Content | Source |
|------|---------|--------|
| 1 | Licence number, or `Unlicensed product!` | bartle:FE4 section 1 |
| 2 | Lowest client level the server accepts | bartle:FE4 section 1 |
| 3 | Highest client level the server supports | bartle:FE4 section 1 |
| 4 | Account ID | bartle:FE4 section 1 |
| 5 | Privilege: 0 guest, 1 normal, 2 wiz, 3 arch-wiz, 4 master, -1 unknown | bartle:FE4 section 1 |

scope: launch

FACT `95 02` brackets two lines, the new account ID and the new privilege level. `95 03` brackets the
account ID on logout and ends game mode.
source: bartle:FE4 Codes and their meanings
source: reference
scope: launch

FACT The reference collects the 95 lines without displaying them and pushes no colour; the lone 255
after the last line closes the bracket.
source: reference
scope: launch

### Dream word (15)

FACT After a `15 00 00` header, Clio and the reference read 1 to 14 lower-case letters as the dream
word; the letters are also displayed. `15 00 01` means the word was said and is used up.
source: clio:src/telnet.l
source: reference
scope: launch

### Snoops (94, 96, 97, 98)

FACT `94` brackets the name of a player whose snooping has begun, `96` marks snooping ended and is
closed at once, `97` brackets snooped material, `98 nn` brackets the prefix of a snooped line.
source: bartle:FE4 Codes and their meanings
scope: launch
