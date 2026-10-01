# Events: the golden-stream schema

FACT This file defines the stream of events a decoder emits for an inbound byte stream. It is written
as JSON Lines so that two independent producers can be diffed: an exporter that maps the reference's
output into it, and the MuckaII decoder under test. Every behaviour in `protocol.md` that can be seen
in a capture is visible as one of these events.
source: director

RULE The JSON block in "Machine-readable schema" at the end of this file is the single source of
truth for event names, key order and value types. The prose tables say the same thing for people.
`scripts/check-goldens.py` validates every golden against the block and checks that the prose tables
name the same events and keys.
enforced_by: scripts/check-goldens.py
source: director

## File format

RULE A golden stream is a UTF-8 file containing only ASCII: one JSON object per line, LF line ends, no
blank lines. The first line is a `stream` record. Any non-ASCII character in a string is written as a
`\uXXXX` escape.
enforced_by: scripts/check-goldens.py
source: director

RULE Every record has `ev` (the event name) and `rx` (the zero-based ordinal of the capture's `rx`
record whose bytes completed the event; `-1` for the `stream` record). Keys are written in the order
listed below. Absent optional values are written as `null`, never omitted. `rx` never decreases from
one record to the next.
enforced_by: scripts/check-goldens.py
source: director

RULE Two streams are equal when they have the same records in the same order and each pair of records
is equal as parsed JSON values. Whitespace and key order do not count.
enforced_by: none: judgement
source: director

## Driving the decoder

RULE A producer feeds the capture one `rx` record at a time, as one network read, after undoing the
capture escapes (`\\`, `\r`, `\n`) and mapping each character to the byte of the same value. `tx` and
`an` records are not fed. After each read it signals end-of-read, which is when a pre-game partial
line is flushed.
enforced_by: none: judgement
source: director

RULE The decoder starts outside game mode, with an empty context stack and the prompt gate open, as
at the start of a connection.
enforced_by: none: judgement
source: reference

RULE The `stream` record says whether the capture begins mid-session. When `start_in_game` is true,
the producer first feeds these 18 bytes as a read of its own, and writes none of the events it causes:

| Bytes | Wire form | Effect |
|-------|-----------|--------|
| `9C 9D FF FF FF FF` | `<01 02></>` | Enters game mode; the stack is empty again |
| `9C FF FF 9C 9D FF FF 2A FF FF FF FF` | `<01><01 02>*</></>` | A prompt shown through the open gate, which closes the gate |

All six captures in `docs/evidence/captures` begin mid-session.
enforced_by: none: judgement
source: director

DECISION After priming, the decoder is in game mode with an empty context stack, no pending text, no
deferred code events, and the prompt gate closed: the state just after a prompt has been shown, which
is where a mid-session capture is most likely to begin. The first prompt of a capture is therefore
shown only if a displayed line ends before it. Rejected: leaving the gate open (the start-of-connection
state), which shows a prompt after a leading FE reply that a running client would have discarded.
source: director

DECISION End of capture is not an event. A producer writes nothing after the last read: no
`game_exit`, no partial, no deferred code events. Rejected: emitting `game_exit` as if the connection
closed, which the capture does not record.
source: director

| `ev` | Keys after `ev`, `rx` |
|------|-----------------------|
| `stream` | `schema` integer, `source` string, `start_in_game` boolean |

FACT `schema` is 2. `source` is `capture:<file>.c1`.
source: director

## Names

DECISION An event or key name must not be a name the reference declares: a type, member, event,
method, enum member, file, or local variable, in its snake_case, PascalCase or camelCase form.
Exempt are names that belong to the outside world: Bartle's own terms (clear screen, end of session,
prompt, exits, inventory, who, dream word, the FES field names) and protocol names (telnet). Common
English words are exempt only as the bare word (`text`, `partial`, `stats`, `code`, `close`), where a
match with a generic member such as a `Text` property carries no borrowed meaning. Rejected: matching
any occurrence of the word anywhere in the reference, which would rule out every plain English word.
source: director

## Shared types

### style

FACT A style is an object `{"fg": F, "bg": B, "attrs": [..]}`.
source: director

| Key | Type | Meaning |
|-----|------|---------|
| `fg` | integer 0-15, or `null` | Foreground colour index (`protocol.md` "Colour indices"); `null` is the terminal default |
| `bg` | integer 0-15, or `null` | Background colour index; `null` is the terminal default |
| `attrs` | array of strings | Attributes that are on, sorted, from `blink`, `bold`, `italic`, `reverse`, `underline`; `[]` when none |

### run

FACT A run is an object `{"text": T, "style": S}`: `text` is a non-empty string, `style` a style.
source: director

RULE Runs and `text` events are maximal: two adjacent pieces of text with equal styles and no other
event between them are one run. A producer whose source splits text at every code merges the pieces
before writing.
enforced_by: none: judgement
source: director

RULE Text never contains CR, LF or NUL. TAB is kept as `\t`. Other bytes 0x01-0x1F and 0x7F are
dropped. A byte 0x80-0x9A is written as the character of the same value (`\u0080`-`\u009a`).
enforced_by: none: judgement
source: reference

### player

FACT A player is an object `{"name": N, "fg": F}`: `name` a string, `fg` an integer 0-15 or `null`.
source: director

## Events

### Text and lines

| `ev` | Keys after `ev`, `rx` | Emitted when |
|------|-----------------------|--------------|
| `text` | `text` string, `style` style | Printable text reaches the current line outside the prompt bracket and outside a suppressed reply |
| `line_end` | `kind` string, `chat_continues` boolean | A line end completes a displayed line |
| `prompt` | `runs` array of run | The outer `01` bracket closes and the prompt gate is open |
| `partial` | `runs` array of run | End-of-read before game mode, with text pending on the line |
| `clear_screen` | (none) | `ESC - C` |
| `erase_eol` | (none) | `ESC - K` |
| `end_of_session` | (none) | `ESC - Q` |
| `width_ack` | `columns` integer | `ESC - <n> W`, or a pre-game `[New terminal width is N]` line |
| `bell` | (none) | BEL |

FACT `kind` is one of `normal`, `chat`, `fight_start`, `fight_end`, `creature_invisible`
(`protocol.md` "Line kinds"). `chat_continues` is true only for a `chat` line whose `09` frame was
open when the line began.
source: reference

FACT A blank line is a `line_end` with no `text` before it. A line that is not displayed (all `*` in
game mode, the width notice, any line inside an FE reply, the FES line) emits no `line_end`.
source: reference

FACT A discarded prompt (gate closed) emits nothing. `text` after a `prompt` belongs to the prompt's
line; joining them is the line model's job, not the decoder's.
source: reference

FACT A `partial` takes the pending text off the line: text after it starts empty.
source: reference

DECISION A `text` event's `rx` is the record holding its last character, and it takes its place in the
stream at that character: an event completed by a later byte comes after it. Rejected: the record of
its first character, and the record of the byte that ends the run (the next style change or line
end), which would move text behind events that the wire sends after it.
source: director

DECISION Text that a `partial` takes off the line is written only inside that `partial`, never also as
`text` events. The `partial` is written at end-of-read, after every other event of that read; an
event that completed while the text was pending comes before it. Before game mode a decoder therefore
holds pending text until it knows whether a line end or end-of-read takes it. Rejected: writing the
text as it arrives and repeating it in the `partial`, which would show it twice.
source: director

DECISION `clear_screen` emits no `line_end`. Text pending before it is written as `text` events before
the `clear_screen`, and the line model commits it (`protocol.md` "Line model"). The clear does not open
the prompt gate and does not end the line for readings: line kind, `89` readings, a pending
`room_name` and deferred code events carry over to the next line end. Rejected: a synthetic
`line_end`, which would open the gate and report readings the reference holds.
source: reference

### Mode

| `ev` | Keys after `ev`, `rx` | Emitted when |
|------|-----------------------|--------------|
| `game_enter` | (none) | A header that enters game mode while outside it, before any text that follows the header |
| `game_exit` | (none) | Any exit trigger in `protocol.md` "Game mode" while inside it |

### Readings

| `ev` | Keys after `ev`, `rx` | Emitted when |
|------|-----------------------|--------------|
| `stats` | `source` string, then the stat keys below | `fes`: the FES line is read. `code`: a displayed line carrying 89 brackets ends |
| `room_name` | `text` string | A displayed line ends whose `02 01` header arrived before any printable text on it |
| `room_long` | `text` string | A displayed line ends while a `02 02` frame is still open |
| `exits` | `exits` array of string | A `12 08 02` bracket closes; the words of every line in it, split on whitespace |
| `inventory` | `room_state` string, `room` array of string, `carried` array of string | A `12 08 03` bracket closes |
| `who` | `players` array of player | A `12 08 05` bracket closes |
| `dreamword` | `word` string or `null` | `15 00 00` with its letters (`word` set), or `15 00 01` (`null`) |
| `client_mode` | `licence` string, `min_level` integer, `max_level` integer, `account` string, `privs` integer | The fifth line of a `95` block |
| `account_change` | `account` string, `privs` integer | The second line of a `95 02` block |
| `persona_unsaved` | (none) | A `08 13` header |
| `teller` | `name` string | A displayed game-mode line carrying a `09 03` header ends, and its label names the sender |

RULE Stat keys are written in this order. For `source` `fes` every key is present (`weather` and
`stamina_colour` may be `null`). For `source` `code` only `stamina`, `max_stamina` and `score` are
present, each `null` unless the line carried that bracket with a valid number.
enforced_by: scripts/check-goldens.py
source: director

| Key | Type | FES field |
|-----|------|-----------|
| `stamina` | integer | 1 |
| `max_stamina` | integer | 2 |
| `strength` | integer | 3 |
| `max_strength` | integer | 4 |
| `dexterity` | integer | 5 |
| `max_dexterity` | integer | 6 |
| `magic` | integer | 7 |
| `max_magic` | integer | 8 |
| `score` | integer | 9 |
| `blind` | boolean | 10 |
| `deaf` | boolean | 11 |
| `crippled` | boolean | 12 |
| `dumb` | boolean | 13 |
| `reset_minutes` | integer | 14 |
| `weather` | one-letter string or `null` | 15 |
| `stamina_colour` | integer 0-15 or `null` | the 99 colour inside field 1 |

FACT `room_name` and `room_long` carry the whole displayed line's text, including text outside the
bracket (the full stop after a room name).
source: reference

open: The server closes `02 02` before the final line end of a long description, so `room_long`
never reports the last row; `dark-two-rats.c1` shows it for both rooms. Kept as the reference does it
so the golden diff holds; a decoder that wants the whole description must read it from the bracket.

FACT `room_name` is the room-entry signal. `protocol.md` "Room" defines entering or looking at a room
as a `02 01` header arriving before any printable text on the line, which is exactly when `room_name`
fires; there is no separate entry event. The reference also treats the uncoded line
`It's too dark to see now` as entering a room. That is a prose reading and belongs to the layer above
the decoder.
source: reference

FACT The reference reports a zeroed stats reading alongside `persona_unsaved`; an exporter drops it.
source: reference

FACT `room_state` is `listed` for names, `blind` for `--`, `dark` for `oo`; `room` is empty unless
`listed`. The `========` line itself appears in neither list.
source: bartle:FE4 FEI (FE INVENTORY)

FACT `who` players are in the order sent; `name` is the bracketed text trimmed (name and rank), `fg`
the colour of its bracket.
source: capture:dark-two-rats.c1

DECISION A player's `fg` is an integer 0-15, or `null` when the bracket's colour is the terminal
default, the same rule as a style's `fg`. Rejected: omitting the key, which breaks the fixed key list,
and writing `-1`, which is not a colour index.
source: director

DECISION A wrapped FE reply is read line by line. In a FEX reply every line's words are exits, so a
wrap changes nothing. In a FEI reply each line is one entry, and a row the server wrapped is not
rejoined to the row before it. In a FEW reply a player's name ends at the first line end inside its
bracket, and the rest of that bracket is dropped. Rejected: rejoining wrapped rows, because a row
exactly as wide as the terminal cannot be told from a wrapped one. Untested: no capture holds a FEI
entry or a WHO name longer than the width it was sent at.
source: reference

FACT `teller` reads the line's label: its text before the first text inside the `09 03` frame, or
before the first `"` if that comes sooner. The label names a sender when it contains ` tells you`
(any case) after its first character and does not begin `You tell `. `name` is the label up to its
first space or tab, or up to ` tells you` if that comes sooner. A label whose name is `Someone` (any
case) gives no event, and so does `Someone powerful tells you`. Only the last `teller` matters to
Ctrl+R (`keys.toml`).
source: reference

FACT Prose readings (stats found in a line's words rather than in an 89 bracket, score deltas,
"Persona saved" totals) are not part of this schema. An exporter whose source merges prose readings
with code readings writes only the values the line's 89 brackets account for.
source: director

### Code events (deferred)

FACT These events report codes whose meaning the layers above need (sounds, the reset countdown, the
status bar). Each is queued when its header is read and written at the next flush point, so that it
lands with the line the code introduces.
source: reference

| `ev` | Keys after `ev`, `rx` | Emitted when |
|------|-----------------------|--------------|
| `sound_code` | `family` integer, `params` array of integer | A header that the table below gives a sound to |
| `reset_signal` | `id` string | A header matching `06 04` (an automatic reset has started its countdown) or `06 06` (the reset taking effect) |
| `spell_phrase` | `id` string, `phrase` string | A header matching `11 00`, `11 01`, `11 02` or `11 03`, once its phrase has been read |

RULE A flush point is a line end that the text path sees (displayed, all `*`, the width notice, or a
line inside an FE reply; not the FES line and not a line of a `95` block), a `partial`, or end-of-read
before game mode. At a flush point the queued events are written in the order their headers arrived;
for one header `sound_code` comes first. `game_exit` discards the queue unwritten. At end of capture
the queue is not written.
enforced_by: none: judgement
source: reference

RULE `sound_code` gives the numbers that sounds.toml's `file-from-code` rule turns into a file name:
`clio.` then `family` and each of `params` as two digits, then `.wav`. The headers that sound, and
their numbers, as sent:

| Header as sent | `family` | `params` | Silent forms |
|----------------|----------|----------|--------------|
| `06`, with any numbers after it | 6 | `[]` | none |
| `07` alone | 7 | `[0, 0]` | `07` with three or more numbers after it |
| `07 a` | 7 | `[a]` | |
| `07 a b` | 7 | `[a, b]` | |
| `08 01 ...`, `08 03 ...` | 8 | `[1]`, `[3]` | every other `08` |
| `11` alone (`11 00`), `11 a` | 11 | `[0]`, `[a]` | `11 06`, `11 09`, `11 14`, and `11` with two or more numbers after it |
| `13 a b` | 13 | `[a]` | `13` with other than exactly two numbers after it |
| `14 03 02` | 14 | `[3, 2]` | every other `14` |
| `18 a`, `a` 00-06 | 18 | `[a]` | every other `18` |

Every other header is silent.
enforced_by: none: judgement
source: reference
source: clio:src/sound.c

FACT Whether a `sound_code` is heard is the sound layer's call, made from sounds.toml: the
`self-act-echo-silent` rule reads the finished line's words (`OK, `), and the gating, volume, override
and fallback rules are settings. The decoder writes every `sound_code` the table gives.
source: director

FACT Text-triggered sounds (sounds.toml rows with `trigger_kind` `text`) and the tell alerts
(`trigger_kind` `event`) read a completed line's words. They belong to the layer above the decoder,
which has the line's `text` events, its `line_end` and any `teller`; no event here reports them.
source: director

FACT `reset_signal` `id` is the matched `c1-codes.toml` id. `ui.md` "Reset countdown" anchors the
countdown at 120 s from `06 04` and treats `06 06` as the reset landing. Minutes to the next reset
arrive in every FES reading as `reset_minutes`.
source: reference
source: bartle:FE4 Codes and their meanings

open: The reference reports `06 04` and `06 06` only when they are sent with exactly two numbers.
`06 02` (a player pressed the reset button), `06 03` (a wizard started a reset) and `06 05` (a reset
was postponed) could also move the countdown; nothing reports them. No capture holds any `06` code.

FACT `spell_phrase` `id` is the matched `c1-codes.toml` id: `11 00` a disabling spell takes hold,
`11 01` it wears off, `11 02` an enhancing spell takes hold, `11 03` it wears off. `phrase` is the
printable text that follows the header, up to the first byte that is not printable ASCII (in every
capture, the bracket's close), with leading and trailing spaces removed. The code fires for a spell
on anyone in the room.
source: bartle:FE4 Codes and their meanings
source: capture:blind-vs-water-snake.c1

FACT Which effect a phrase describes, and whether it is the player's own, is read from its words:
`ui.md` "Effect icons" gives the rules (own phrases begin `You `, `Your ` or `Some of your `; the stat
and direction come from words such as `fitter` and `less adroit`). That is the status bar's layer, not
the decoder's. Deaf, blind, dumb and crippled are on or off from the FES reading's flags.
source: reference

open: `11 20` and `11 21` (the stronger disabling spells) carry no `spell_phrase`, as in the
reference. No capture holds either.

### Ordering

RULE Events appear in the order of the byte that completes them, with these fixed orders where several
complete on one byte:

| Byte | Order |
|------|-------|
| The line end of a displayed line | after the line's `text`: deferred code events, `teller`, `stats` (code), `room_name`, `room_long`, `line_end` |
| A line end that is not displayed | deferred code events |
| End-of-read before game mode | deferred code events, then `partial` |
| The lone 255 that closes the outer `01` | any reply event closing on the same byte, then `prompt` |
| A header that enters game mode | `game_enter` before the `push` wire event |

enforced_by: none: judgement
source: reference

### Wire tier

FACT These events report the context stack directly. A Rust decoder emits them for coverage tests (each
`code` id counted against `observed` in `c1-codes.toml`). They are written to a separate file, or
filtered out before a golden diff, because the reference exposes no equivalent. A golden holds none.
source: director

RULE The wire tier reports every header and close in the stream, including those inside FE replies
and inside the FES line, whose bytes the reference reads as raw data. The `observed` counts in
`c1-codes.toml` include them.
enforced_by: none: judgement
source: capture:blind-vs-water-snake.c1

| `ev` | Keys after `ev`, `rx` | Emitted when |
|------|-----------------------|--------------|
| `code` | `id` string, `raw` string, `op` string | Every C1 header: `id` the matched `c1-codes.toml` id or `""`, `raw` the numbers as sent, `op` its `stack` value |
| `close` | `popped` boolean | Every lone 255 that is not the end of a header; `popped` is false for a surplus close |
| `telnet` | `bytes` string | A telnet command, as space-separated hex |

FACT `raw` is written like `"08 00"`.
source: director

## Example

EVIDENCE The second `rx` record of `blind-vs-water-snake.c1` is
`<04 00 03>The water-snake1 slithers away.</>\r\0\r\n<01>(<01 02>*</>)</>`.
source: capture:blind-vs-water-snake.c1

FACT Its golden events:
source: director

```
{"ev":"text","rx":1,"text":"The water-snake1 slithers away.","style":{"fg":5,"bg":0,"attrs":[]}}
{"ev":"line_end","rx":1,"kind":"normal","chat_continues":false}
{"ev":"prompt","rx":1,"runs":[{"text":"(","style":{"fg":4,"bg":0,"attrs":[]}},{"text":"*","style":{"fg":12,"bg":0,"attrs":[]}},{"text":")","style":{"fg":4,"bg":0,"attrs":[]}}]}
```

FACT The third record (`<12 08 01>` ... `</><01>(<01 02>*</>)</>`) gives one `stats` record with
`source` `fes`, `stamina` 120, `max_stamina` 120, `strength` 97, `weather` `"T"`, `stamina_colour` 10,
and nothing else: the FES line is not displayed and the prompt that follows it is discarded.
source: capture:blind-vs-water-snake.c1

EVIDENCE Record 24 of the same capture ends
`<11 00>You have suddenly and magically gone blind!</>\r\0\r\n<01>(<01 02>*</>)</>`.
source: capture:blind-vs-water-snake.c1

FACT Its last line gives `text`, then `{"ev":"sound_code","rx":24,"family":11,"params":[0]}`, then
`{"ev":"spell_phrase","rx":24,"id":"11 00","phrase":"You have suddenly and magically gone blind!"}`,
then `line_end`.
source: director

## Machine-readable schema

FACT Types in the block: `int`, `bool`, `string`; `text` is a non-empty string; `colour` an integer
0-15; `letter` a one-character string; `attrs` a sorted array of distinct attribute names;
`{"nullable": T}`, `{"array": T}` (with an optional `max` length) and `{"enum": [...]}` build on
them; `style`, `run` and `player` name the object types in `types`. Each event lists its keys after
`ev` and `rx`, in order. `stats` has one key list per `source`. Events under `wire` never appear in a
golden.
source: director

```json
{
  "schema": 2,
  "types": {
    "style": [["fg", {"nullable": "colour"}], ["bg", {"nullable": "colour"}], ["attrs", "attrs"]],
    "run": [["text", "text"], ["style", "style"]],
    "player": [["name", "string"], ["fg", {"nullable": "colour"}]]
  },
  "attrs": ["blink", "bold", "italic", "reverse", "underline"],
  "events": {
    "stream": [["schema", "int"], ["source", "string"], ["start_in_game", "bool"]],
    "text": [["text", "text"], ["style", "style"]],
    "line_end": [["kind", {"enum": ["normal", "chat", "fight_start", "fight_end", "creature_invisible"]}], ["chat_continues", "bool"]],
    "prompt": [["runs", {"array": "run"}]],
    "partial": [["runs", {"array": "run"}]],
    "clear_screen": [],
    "erase_eol": [],
    "end_of_session": [],
    "width_ack": [["columns", "int"]],
    "bell": [],
    "game_enter": [],
    "game_exit": [],
    "stats": {
      "fes": [["source", {"enum": ["fes"]}], ["stamina", "int"], ["max_stamina", "int"], ["strength", "int"], ["max_strength", "int"], ["dexterity", "int"], ["max_dexterity", "int"], ["magic", "int"], ["max_magic", "int"], ["score", "int"], ["blind", "bool"], ["deaf", "bool"], ["crippled", "bool"], ["dumb", "bool"], ["reset_minutes", "int"], ["weather", {"nullable": "letter"}], ["stamina_colour", {"nullable": "colour"}]],
      "code": [["source", {"enum": ["code"]}], ["stamina", {"nullable": "int"}], ["max_stamina", {"nullable": "int"}], ["score", {"nullable": "int"}]]
    },
    "room_name": [["text", "string"]],
    "room_long": [["text", "string"]],
    "exits": [["exits", {"array": "string"}]],
    "inventory": [["room_state", {"enum": ["listed", "blind", "dark"]}], ["room", {"array": "string"}], ["carried", {"array": "string"}]],
    "who": [["players", {"array": "player"}]],
    "dreamword": [["word", {"nullable": "string"}]],
    "client_mode": [["licence", "string"], ["min_level", "int"], ["max_level", "int"], ["account", "string"], ["privs", "int"]],
    "account_change": [["account", "string"], ["privs", "int"]],
    "persona_unsaved": [],
    "teller": [["name", "text"]],
    "sound_code": [["family", "int"], ["params", {"array": "int", "max": 2}]],
    "reset_signal": [["id", {"enum": ["06 04", "06 06"]}]],
    "spell_phrase": [["id", {"enum": ["11 00", "11 01", "11 02", "11 03"]}], ["phrase", "string"]]
  },
  "wire": {
    "code": [["id", "string"], ["raw", "string"], ["op", "string"]],
    "close": [["popped", "bool"]],
    "telnet": [["bytes", "string"]]
  }
}
```
