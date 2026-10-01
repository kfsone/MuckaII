# input

FACT This file says how the command box behaves: Enter, the ordered input queue, history, F-key
macros, aliases and `^1`-`^3` slots, every `$` client command, watchwords, scrollback, chat mode
and mobile differences. Key chords themselves are listed in `keys.toml`.
source: director

## Invariant #1 and its acceptance condition

RULE Typed text and Enter form one ordered stream, all the way to the socket. When the sent lines
are concatenated, they equal the typed stream.
enforced_by: none: judgement
source: director

RULE (derived from the invariant above) For any keystrokes typed at any speed (the director types
at 120-130 wpm), no character is lost, duplicated, reordered, or carried from one line into the
next, and Enter always ends exactly the line typed before it.
enforced_by: none: judgement
source: director, reference (the four failure cases at the end of this file)

open: Where is the acceptance condition measured? Several rules in this file change the bytes on
purpose: the line is trimmed (typing `  look ` sends `look`); `$` and `^` lines are handled locally
and may send nothing or something else; aliases and `$VER` expand; `$$` collapses; the 7-bit
boundary transliterates; scrollback swallows typed keys. Candidates: (a) at the handoff from the
box to the queue, before any of that; (b) at the socket, counting only plain untrimmed ASCII lines
with no sigil. Should trimming go?
source: reference

## Enter

RULE Enter does three things in this order: read the box's text, empty the box, hand the line to
the input queue. The box is empty before any work on the line begins. Text typed after Enter
starts the next line, never the end of this one.
enforced_by: none: judgement
source: reference

| Line typed | What happens | Bytes on the wire |
|---|---|---|
| empty, or whitespace only | Sent. A bare Enter is a real MUD2 action and is never swallowed. Not added to history. | `CR LF` |
| starts with `$$` or `^^` | Not a client command. Sent as text with the doubled sigil collapsed (see Expansion). | `$...` or `^...` then `CR LF` |
| starts with a single `$` | Client command, alias definition, alias run or watchword speak (see `$` commands). Never sent as typed. | only what the command sends |
| starts with a single `^` | Control-macro definition or run (see Aliases). Never sent as typed. | only the slot body, if run |
| anything else | Trimmed, expanded (see Expansion), sent. | `<expanded line> CR LF` |
source: reference

RULE The line is trimmed (leading and trailing whitespace removed) before any of the rows above
are decided.
enforced_by: none: judgement
source: reference

RULE A Send button beside the box performs exactly what Enter performs, through the same path.
enforced_by: none: judgement
source: reference

RULE Outbound text is 7-bit ASCII, enforced at the single send boundary. Typographic punctuation is
transliterated and anything else is dropped.
enforced_by: none: judgement
source: director

EVIDENCE clio has an option (`-e`) that makes a blank input line repeat the last command. The
reference has no such option; a blank line sends a bare CR LF.
source: clio:man/clio.6

## Ordering: one queue

RULE Every player-initiated action goes through one first-in, first-out queue, in the order the
player caused it, to one socket writer. The queue holds: typed lines, F-key and on-screen F-key
presses, Ctrl hotkeys (dreamword, flee, wield, `^n` slots), compass clicks, and requests to change
the box (history recall, Ctrl+R prefill, name insert, Escape clear, focus).
enforced_by: none: judgement
source: director, reference

RULE A hotkey pressed after Enter takes its turn behind that line. A box change (recall, prefill)
cannot land between a keystroke and its Enter.
enforced_by: none: judgement
source: reference

RULE Work that an item creates while it is being handled (a client command that sends a line)
runs after that item and before anything queued later.
enforced_by: none: judgement
source: reference

RULE One item that fails does not drop the items queued behind it.
enforced_by: none: judgement
source: reference

RULE The keepalive (an empty line sent after the idle interval, see `connection.md`) also waits
behind any queued line.
enforced_by: none: judgement
source: reference

RULE Recalling a history entry always puts the stored text in the box, even when the box shows an
edited copy of that entry or the same entry already. Escape always leaves the box empty.
enforced_by: none: judgement
source: reference

## History

RULE Command history behaves as in the Reference column below. The clio column is for comparison.
enforced_by: none: judgement
source: reference, clio:src/history.c

| Property | Reference (Mucka) | clio |
|---|---|---|
| What is stored | Every accepted non-empty line after trimming, including `$` and `^` client commands, as typed (before expansion). F-key, hotkey and click sends are not stored. | Every accepted non-empty line |
| Size | 200, oldest dropped | 40, ring buffer |
| Duplicates | Stored every time | A line equal to the newest entry is not stored again |
| Position after any Enter (including an empty one) | Reset to just past the newest | Reset to just past the newest |
| Up at the oldest entry | Stays on the oldest | Wraps to the newest |
| Down past the newest entry | Empties the box. After any Enter the position is already past the newest, so the first Down with no recall in progress empties the box: type `kill ra`, press Down, and the draft is gone. | Wraps to the oldest |
| Unsent draft when Up is pressed | Discarded; not restored by Down | Discarded |
| Recalled line | Placed in the box, caret at end, fully editable; editing does not change the stored entry; Enter sends the edited text as a new entry | Same |
| Lifetime | The app run; not saved to disk | The app run |
source: reference, clio:src/history.c, clio:src/telnet.l

EVIDENCE clio `hadd` ignores a line equal to the most recent entry; `hprevious`/`hnext` step
around a 40-slot ring and skip empty slots.
source: clio:src/history.c

open: history size, consecutive-duplicate suppression, and wrap versus clamp at the ends. The
reference and clio disagree on all three.
source: reference, clio:src/history.c

open: Down with no recall in progress destroys the typed draft. An accidental Down at typing speed
loses the line. Keep, or make Down a no-op (or stash the draft) when not recalling?
source: reference

## Escape

RULE Escape acts on the first row that applies.
enforced_by: none: judgement
source: reference

| Context | Effect |
|---|---|
| live, About overlay open | Close the overlay |
| live, score graph panel open | Close the panel |
| live, otherwise | Empty the box |
| scrollback | Return to live |
source: reference

## Box prefills

RULE These are the only ways the client puts text into the box. Each goes through the queue (see
Ordering).
enforced_by: none: judgement
source: reference

| Trigger | Box becomes | Caret | Condition |
|---|---|---|---|
| Up / Down | the history entry | end | - |
| Ctrl+R | `<last teller> "` (replaces anything typed) | end | a named tell has been received this connection |
| click on a player name in the terminal | `<name> ` prepended to what is typed | after the inserted name | caret at the start of the box and nothing selected; otherwise ignored |
source: reference

open: `ui.md` 2.5 says tapping a tell sender's name inserts `Name ` only when the box is empty; the
row above prepends it to typed text whenever the caret is at the start. One rule or two?

## F-key macros

FACT There are 36 macro slots: F1-F12 (slots 1-12), Shift+F1-F12 (slots 13-24), Ctrl+F1-F12
(slots 25-36). This is the clio.ini and mucka.ini `[fkeys]` layout (`F1=` .. `F36=`), see
`mucka-ini.md`.
source: reference, clio:man/clio.6 FUNCTION KEYS

### Default macros

RULE When the setting `defaulthotkeys` is on (the default), an empty slot in this table uses the
built-in default. A slot the user filled keeps the user's text.
enforced_by: none: judgement
source: reference

| Slot | Chord | Default text |
|---|---|---|
| 1 | F1 | `qscan` |
| 2 | F2 | `wield best weap` |
| 3 | F3 | `open bag,g 1 wafer from bag,eat 1 wafer` |
| 4 | F4 | `flee o` |
| 5 | F5 | `qw,qs` |
| 6-36 | - | empty |
source: reference

### Macro text syntax

RULE Macro text is interpreted as follows when its key is pressed.
enforced_by: none: judgement
source: reference

| Element | Behaviour |
|---|---|
| Command separator | None in the client. A macro is always sent as one line. A comma (`a,b`) is sent verbatim; MUD2 itself treats it as a separator. |
| Embedded Enter | None. Trailing CR and LF characters are stripped before sending. There is no syntax for a line break inside a macro. |
| Empty or whitespace-only slot | Nothing is sent. |
| Macro text starting with `$` | Not a client command and not an alias: the rest of the text is a watchword slot name, spoken as for `$<slot>` (see Watchwords). |
| `$name` anywhere else | Replaced by the session alias `name` (see Expansion). |
| `$VER` | Replaced by the version string. |
| `^1`, `^2`, `^3` | Replaced by the body of that control-macro slot. |
| `$<watchword slot>` | Replaced by the slot's queued answer (scope later). |
| `$$`, `^^` | A literal `$` or `^`. |
| Anything else | Sent verbatim. |
source: reference

RULE An F-key send is not added to command history.
enforced_by: none: judgement
source: reference

EVIDENCE clio sends a macro verbatim with CR LF, and sends nothing when the text is empty or
longer than 254 characters. It has no expansion of its own in F-key text.
source: clio:src/fkey.c

### On-screen F-key bar

RULE A row of F-key buttons sits above the command box. Tapping a button sends that slot's macro
from the plain layer (slots 1-12 only) and returns focus to the box. The bar can be hidden and
shown with a toggle.
enforced_by: none: judgement
source: reference

| Terminal columns in use | Buttons shown | Label |
|---|---|---|
| 76 or more | 12 | `F1`..`F12` |
| 63-75 | 11 | `F1`..`F11` |
| 50-62 | 10 | `F1`..`F10` |
| 38-49 | 9 | `1`..`9` |
| under 38 | 8 | `1`..`8` |
source: reference

### F-key editor

RULE The editor shows three tabs, None, Shift and Ctrl, each with twelve rows labelled F1-F12
holding the macro text for that layer. Save to profile only (otherwise global) is a checkbox. On
desktop an Import button reads a clio.ini and fills all 36 slots from its `F1=`..`F36=` keys,
clearing slots the file does not set. While the editor is open, every hotkey is inert.
enforced_by: none: judgement
source: reference, clio:src/fkeys.c

## Aliases

FACT Aliases are session commands the player defines by typing. They live until the player leaves
the gameworld: they are cleared when game mode ends and on disconnect. They are not saved.
source: reference

### Forms

| Typed | Effect | Output line |
|---|---|---|
| `$name=command` | Define alias `name`. Whitespace around `=` is ignored. Redefining replaces. | `[command] $name = <body>` |
| `$name` | Send the alias body, expanded. Name lookup ignores case. | - |
| `^1=command` .. `^3=command` (also `^1 = command`) | Define control-macro slot 1-3. | `[command] ^1 = <body>` |
| `^1` .. `^3` alone | Send the slot body, expanded. Undefined slot: nothing sent, no message. | - |
| Ctrl+1 .. Ctrl+3 (desktop) | Same as typing `^1` .. `^3`. | - |
| any other line starting with a single `^` (`^4=x`, `^=x`, `^`, `^1x`) | Nothing sent. | `[command] unrecognized "^" command: <line>` (error colour) |
source: reference

### Definition rules

RULE A definition that breaks a rule below is refused and prints, in the error colour,
`[command] cannot define $name: <error>` (for a slot: `[command] cannot define ^1: <error>`). The
line is not sent.
enforced_by: none: judgement
source: reference

| Rule | Error text |
|---|---|
| Name is `^1`-`^3`, or starts with an ASCII letter and continues with letters, digits or `_` | `name must start with a letter and contain only letters, digits, or underscores` |
| Name is not reserved: `help`, `?`, `<`, `con`, `map`, `fkeys`, `VER` (exact case), `SID`, `MARK`, `SCORE`, `CHATTEST` (any case), or `f`/`F` followed by an integer | `cannot replace built-in $<name>` |
| Body is not empty | `command cannot be empty` |
| Body references no built-in other than `$VER` (for example `$help`, `$f1`, `$map`) | `cannot use built-in $<name> in a command definition` |
source: reference

RULE References in a body to aliases that already exist are expanded once, at definition time
(the body snapshots them). References to names not yet defined are kept and expanded when the
alias runs. `$VER` in a body is expanded at definition time. `$$` and `^^` in a body stay literal
and are shown doubled when the body is echoed.
enforced_by: none: judgement
source: reference

### Expansion of outgoing text

RULE Every line sent from the box (after client-command handling), every F-key macro, and every
alias or slot body is expanded in this order: (1) aliases, `$VER`, and bare `^1`-`^3` anywhere in
the line; (2) watchword `$<slot>` references; (3) collapse `$$` to `$` and `^^` to `^`. The collapse
runs last so an escaped `$$gold` is never seen by the watchword pass.
enforced_by: none: judgement
source: reference

| Input | Sent | Why |
|---|---|---|
| `say $VER` | `say Mucka v<version>` | `$VER` is case-sensitive; `$ver` is left alone |
| `say ^1 now` with `^1=look` | `say look now` | bare `^n` expands mid-line |
| `say $$VER` | `say $VER` | doubled sigil is a literal |
| `$$` | `$` | |
| `say $$$VER` | `say $Mucka v<version>` | pairs collapse left to right, the odd one interpolates |
| `say ^^1` | `say ^1` | |
| `say $nosuchalias` | `say $nosuchalias` | unknown names pass through |
| `^9` mid-line | `^9` | only `^1`-`^3` are slots |
source: reference

open: `$VER` expands to `Mucka v<version>` in the reference. What does MuckaII send?
source: reference

### Positional arguments (defined, rarely reached)

FACT The reference defines positional arguments: an alias body may use `$1`-`$9`. When a line starts with `$name` and the body uses a slot,
the words after the name fill the slots; a missing argument becomes empty; words past the highest
slot used are appended after a space; arguments are not themselves expanded; `$10` is `$1` then
`0`. A body with no slots appends the rest of the line. Examples: `$k=ql $1,k $1 wi $2` then `$k
rat0 axe` sends `ql rat0,k rat0 wi axe`; `$g=get` then `$g sword` sends `get sword`. `$$1` in a
body is a literal `$1`.
source: reference

open: In the reference, a typed line `$g sword` is looked up as the alias name `g sword`, misses,
and falls to the watchword speak (`[watchword] nothing queued for $g sword`); nothing is sent. The
positional and appending forms above are reachable in practice only inside another body that
refers to an alias defined later. Is the intended behaviour above what MuckaII should do?
source: reference

## `$` commands

RULE A typed line starting with a single `$` is always handled locally and never sent as typed.
The checks run in this order: alias definition (`=` anywhere in the line), then the table below
top to bottom, then alias run, then watchword speak.
enforced_by: none: judgement
source: reference

RULE Client output lines are printed in the info colour (palette index 14) unless marked error
(palette index 9). Lines marked "logged" are also written to the wire log as annotations. The
commands behave as in the table.
enforced_by: none: judgement
source: reference

| Command | Name match | Syntax | Effect and output | Scope |
|---|---|---|---|---|
| `$help` | exact lowercase | `$help` | Print the help block below | launch |
| `$?` | exact | `$?` | `[watchword] $a  $b ...` (slot names, two spaces apart) or `[watchword] no slots loaded` | later |
| `$<` | exact | `$<` | Scan the last 80 committed terminal lines (blank lines skipped, joined by spaces) for watchword triggers; per match `[watchword] queued "<answer>" -> $<slot>`; none: `[watchword] no matches in recent history` | later |
| `$con` | exact lowercase | `$con` | Open the raw protocol console (desktop). Elsewhere: `[command] $con is Windows only.` (error) | later |
| `$fkeys` | `fkeys` exact; `fkeys <layer>` any case | `$fkeys [shift\|ctrl]` | Logged. `[fkeys] F1-F12:` (or `Shift+F1-Shift+F12:`, `Ctrl+F1-Ctrl+F12:`), then `  <label><n> = <macro>` for each non-empty slot, where label is `F`, `Shift+F` or `Ctrl+F` and n is 1-12, or `  (none set)`. Unknown layer: `[fkeys] unknown layer '<x>' (use: shift \| ctrl)` (error) | launch |
| `$f<n>` | `f` or `F` then an integer | `$f1` .. `$f36` (absolute slot) | Insert the note `// <macro>` above the live prompt (prompt redrawn below it), in bright green (palette 10); logged. Errors: `[fkey] <n> out of range (1-36)`, `[fkey] <n> is empty` | launch |
| `$SID` | any case | `$SID` | `[sid] s=<host>/m=<client run id>/p=<persona>[@<reset number>]/i=<login id>`; each unknown part reads `unknown` | later |
| `$CHATTEST` | any case | `$CHATTEST [slow\|stop]` | Play a scripted chat into the terminal as the current persona; sends nothing, stays out of the transcript, and never sets the Ctrl+R teller. `slow`: one script line at a time, each after 0.5 s plus an exponential delay of mean 1 s, capped at 9 s; notes `// $CHATTEST slow: ...` on start, finish and stop. `stop`: end a slow run, or `// $CHATTEST: nothing is running`. Needs a persona: `[chattest] needs a persona in the game - the script speaks as you` (error). Bad argument: `[chattest] unknown argument '<x>'; usage: $CHATTEST [slow\|stop]` (error). A slow run stops on game-mode exit and disconnect. | later |
| `$MARK` | any case | `$MARK [@id\|"note"]`, `$MARK start [@id\|"note"]`, `$MARK end [@id\|"note"]` | Logged landmark. Bare: `//MARK: <id>`. `start`: `//BEG: <id>`, opens a span; spans nest to depth 8. `end`: `//END: <id>`, closes the innermost (with an id it must match the innermost). Ids: `mm-<login id>:<id>` when supplied, `mm-<login id>-<8 hex>` when generated. `@id` is a letter or digit then letters, digits, `.`, `_`, `-`. A note is quoted, 1-64 characters, no inner quote, nothing after it. On login end every open span is closed as `//END: <id> (<reason>)`, default reason `session ended`. Errors start `ERROR: ` and are not logged: needs a login (`$MARK needs an open persona session - you are not in game mode.`), `Too many open marks (maximum 8).`, `No open marks to close.`, `<id> is not the current mark (<current>)` (plus a hint to use `$MARK end` when the current one was generated), unterminated, trailing text, empty or over-long note, `Expected @id or a "quoted note", got: <x>` | later |
| `$SCORE` | any case | `$SCORE [days] [name]`, either order | Open the score graph panel for `name` (default: current character) over `days` (default 3, clamped 1-90). Error: `[score] <reason>; usage: $SCORE [days] [name]` (error) | later |
| `$VER` | exact uppercase | `$VER` | Send the version string as a line: `Mucka v<version>` | launch |
| `$map` | - | - | Reserved name with no command; falls to watchword speak | - |
| `$<alias>` | any case | `$<alias>` | Send the alias body, expanded | launch |
| `$<anything else>` | - | `$<slot>` | Watchword speak: send `"<answer>` (a leading quote, MUD2's say) and clear the slot; nothing queued: `[watchword] nothing queued for $<slot>` | later |
source: reference

open: Scope of `$SID`, `$MARK` and `$CHATTEST`. They serve the wire log (launch) but are
diagnostics; marked later here.
source: reference

### `$help` output

RULE `$help` prints the block below, verbatim on desktop. The `$VER` line names the client and
follows the `$VER` open item.
enforced_by: none: judgement
source: reference

```
[help] client commands:
  $help                 list these commands
  $?                    list loaded watchword slots
  $<                    scan recent output for watchword answers
  $con                  open the raw protocol console
  $fkeys [shift|ctrl]   list your function-key macros
  $f<n>                 annotate output with fkey n's text (1-36)
  $SID                  print the current session id (server/run/persona/login)
  $CHATTEST             play a scripted chat into the output (sends nothing)
  $CHATTEST slow|stop   play it one line at a time, ~1.5s apart / stop that
  $MARK [@id|"note"]    drop a landmark into the log
  $MARK start|end ...   open/close a nested span (end takes no id)
  $SCORE [days] [name]  graph a character's score (default: 3 days, you)
  $VER                  expands to the current Mucka version
  $name=command         define a command until you exit the gameworld
  $name                 run a command defined above
  ^1/^2/^3=command  bind Ctrl-1..Ctrl-3
  $$ / ^^               doubled sigil sends a literal $ or ^ (no interpolation)
```

FACT On Android, the `$con` line reads `(Windows only)` and the slot line reads
`^1/^2/^3=command  bind a slot; type ^1 to run it`.
source: reference

## Watchwords (scope: later)

FACT Watchwords answer puzzles: the player configures a trigger text; `$<` scans recent output
for it and queues an answer under a slot name; the answer is later spoken with `$<slot>` or
substituted into outgoing text. Nothing scans automatically.
source: reference

RULE The rules live in `mucka.ini` and are hand-written; the client never writes them. They are
imported verbatim (`INTERNAL.md`, Data).
enforced_by: none: judgement
source: reference, director

RULE The `[watch]` sections of `mucka.ini` are read as follows.
enforced_by: none: judgement
source: reference

| Section | Line format | Meaning |
|---|---|---|
| `[watch]` | `slot = prefix...suffix` | A slot whose capture is the text between `prefix` and `suffix`. With no `...`, the whole value is the prefix and the capture runs to the end. |
| `[watch <slot>]` | `key = answer` | Exact match: if the capture equals `key`, queue `answer`. |
| `[watch <slot>]` | `key... = answer` | Prefix match: if the capture starts with `key`, queue `answer`. |
| `[watch <slot>]` | `"key" = answer` | Surrounding quotes on a key are removed. |
| (none) | - | A slot with no `[watch <slot>]` section is in echo mode: the capture itself is queued. |
source: reference

RULE Watchword parsing and matching follow these limits.
enforced_by: none: judgement
source: reference

| Limit or detail | Value |
|---|---|
| Separator | ` = ` (space, equals, space); lines without it are ignored |
| Comments | lines starting `;` or `#` |
| Section and slot names | case-insensitive |
| Max slots | 20 |
| Max answers per slot | 32 |
| Max capture | 80 characters (longer captures are cut, not rejected) |
| Max answer | 48 characters |
| Whitespace | every run of whitespace in triggers, keys and scanned text is collapsed to one space before matching; the capture is trimmed |
| Queue | one answer per slot; speaking or substituting it clears it |
source: reference

## Scrollback

RULE Scrollback is an explicit mode that behaves as follows.
enforced_by: none: judgement
source: reference

| Aspect | Behaviour |
|---|---|
| Enter scrollback | PageUp; mouse wheel up (3 rows per whole notch; small touchpad deltas accumulate); a mouse drag past a small threshold, which also starts a text selection; on touch, a finger drag. A plain click never enters scrollback. |
| While in scrollback | The view is frozen on the lines held at entry; new output is buffered but not painted. A banner covers the input row, `SCROLLBACK - Esc or click to return to live`, and the row's height does not change. |
| Typing | Blocked: keys other than those in `keys.toml` (scrollback context) are swallowed. |
| Hotkeys | Ctrl+D, Ctrl+Shift+D, Ctrl+F, Ctrl+Shift+F, Ctrl+1..3 return to live, then act. |
| Copy | Ctrl+C or a right-click copies the selection; toast `* Copied to clipboard`. A press starts a new selection. |
| Leave scrollback | Scrolling back to the bottom, End, Escape, clicking the banner, any hotkey above, or a server clear-screen. Focus returns to the command box. |
| Clicks in live view | A click that activates or refocuses the window only focuses the box; it does not insert a name or start a selection. A click on a player name inserts it (see Box prefills). |
source: reference

open: Whether F-keys, Ctrl+R, Ctrl+W and Ctrl+` act in scrollback. The reference's scrollback key
filter does not list them and appears to swallow them.
source: reference

open: Scrollback swallows typed keys, which conflicts with Invariant #1. See `keys.toml`.
source: reference

EVIDENCE clio scrolls back with PageUp and PageDown, returns to live with Ctrl-A, and keeps a
300-line buffer by default.
source: clio:man/clio.6 SCROLL BACK

## Chat mode

RULE A latching Chat toggle filters the terminal to chat lines only (lines the server marks as a
speaker message: shout, say, tell, emote; see `protocol.md`). It is a view filter only: typing,
Enter, hotkeys and everything sent are unchanged, and nothing is prefixed to outgoing lines.
enforced_by: none: judgement
source: reference

RULE Chat mode behaves as follows.
enforced_by: none: judgement
source: reference

| Aspect | Behaviour |
|---|---|
| Turning on | The terminal repaints from a chat-only history, up to 3000 lines deep, so older chat survives. |
| Turning off | The terminal repaints the full history as it would stand had the filter never been on. |
| Cues while on | Box placeholder `chat...` (otherwise `enter command...`); the box frame turns yellow; the toggle is lit and flashes amber when non-chat output arrives. |
| Ends | When game mode ends and on disconnect. |
| Focus | Toggling returns focus to the box. |
source: reference

open: Scope of chat mode (not on the INTERNAL.md launch list). Marked later here.
source: reference

## Mobile specifics

RULE On Android the command box differs from desktop as follows.
enforced_by: none: judgement
source: reference

| Aspect | Android behaviour |
|---|---|
| Ctrl+1..3 | Not available on a soft keyboard. Type `^1`, `^2` or `^3` and Enter to run a slot. |
| Hardware keyboard | Only F1-F12 (with Shift and Ctrl layers), Ctrl+D, Up, Down and Escape are bound. Ctrl+Shift+D speaks the plain dreamword. |
| F-keys | The on-screen F-key bar (plain layer) is the main way to send macros. |
| `$con` | Prints `[command] $con is Windows only.` |
| Watchwords | Effectively unavailable in the reference: its ini was app-private, so nobody could hand-edit `[watch]`. |
| Scrollback | Finger drag; no PageUp. |
source: reference

## Typing-path failure cases

RULE The acceptance condition above catches each of these cases, which the reference failed.
enforced_by: none: judgement
source: reference

| # | Trigger | Wrong outcome |
|---|---|---|
| 1 | A character and Enter typed within about 10 ms | The last character is left in the box and joins the next line (`n` Enter `n` gives `nn`) |
| 2 | Enter when the read of the box comes back empty | Typed text is left behind and prepended to the next command |
| 3 | `n` Enter `ne` Enter | `nne` is sent |
| 4 | Up | History appears to skip an entry |
source: reference
