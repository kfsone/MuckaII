# input

Key chords: `keys.toml`. Later features: `later/README.md`.

## Invariant #1 and its acceptance condition

The invariant and its two checkpoints are root `CLAUDE.md`, Invariant #1.

RULE For keystrokes typed at any speed, no character is lost, duplicated, reordered, or carried
from one line into the next, and Enter always ends exactly the line typed before it.
enforced_by: none: judgement
source: director, reference (the four failure cases at the end of this file)

open: Where is the acceptance condition measured? Several rules here change the bytes on purpose:
trimming (`  look ` sends `look`); `$` and `^` lines handled locally; alias and `$VER` expansion;
`$$` collapse; 7-bit transliteration; scrollback swallowing keys. Candidates: (a) the box-to-queue
handoff, before any of that; (b) the socket, counting only plain untrimmed 7-bit lines with no
sigil. Should trimming go?
default: both checkpoints of root `CLAUDE.md` Invariant #1: (1) the text handed over at each Enter
is the line buffer at that Enter, untrimmed and unexpanded; (2) with marked probe blocks removed,
the socket bytes equal the client's documented output. Trimming stays, as documented processing.

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

Outbound text passes the 7-bit send boundary: root `CLAUDE.md`, Bytes, not strings.

EVIDENCE clio has an option (`-e`) that makes a blank input line repeat the last command. The
reference has no such option; a blank line sends a bare CR LF.
source: clio:man/clio.6

## Ordering: two orderings

The mechanism (UI loop over the line buffer; `core::submitLine` into the one FIFO; client expands
and filters; session alone inserts probes) is `architecture.md`, Input: two orderings.

FACT What each ordering holds:

| Ordering | Holds | Ordered by | Crosses a thread |
|----------|-------|------------|------------------|
| Box edits | typed keystrokes, history recall, Ctrl+R prefill, name insert, Escape clear, focus | the UI loop, applying input events to the frontend-owned line buffer in arrival order | never |
| Sends | the line at Enter or Send, F-key and on-screen F-key presses, Ctrl hotkeys (dreamword, flee, wield, `^n` slots), compass clicks | `core::submitLine`, which pushes onto the one UI -> core FIFO in UI arrival order | once, UI to core |

source: agent

RULE A box change (recall, prefill) cannot land between a keystroke and its Enter; a hotkey pressed
after Enter takes its turn behind that line.
enforced_by: none: judgement
source: agent, reference

RULE Work that an item creates while it is being handled (a client command that sends a line)
runs after that item and before anything queued later.
enforced_by: none: judgement
source: reference

RULE One item that fails does not drop the items queued behind it.
enforced_by: none: judgement
source: reference

RULE The keepalive (an empty line sent after the idle interval, `scope: later`, see
`later/anti-idle.md`) also waits behind any queued line.
enforced_by: none: judgement
source: reference

RULE Recalling a history entry puts its text in the box: the stored text, or the edited copy of it
the user left behind with Up or Down since the last Enter (see History, DECISION). Escape always
leaves the box empty.
enforced_by: none: judgement
source: reference; agent (edited copies, on a review finding)

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
default: the Reference column: 200 entries, oldest dropped; a line is stored every time, duplicates
included; Up clamps at the oldest entry and Down stops past the newest, with no wrap. This is taste,
so the reference decides.
source: reference, clio:src/history.c

open: Down with no recall in progress destroys the typed draft. An accidental Down at typing speed
loses the line. Keep, or make Down a no-op (or stash the draft) when not recalling?
default: Down with no recall in progress is a no-op, and the draft stays. Typed text is never
discarded by one key (Invariant #1). Down during a recall behaves as in the table, except that Up
stashes the unsent draft and Down past the newest entry restores it rather than emptying the box
(the "Unsent draft" row's discard is a lost draft too). Built so in `client::LineEditor`.
source: reference; agent (the stash, on a review finding: Up then Down lost the draft)

DECISION No key but Enter and Escape discards typed text. The draft and each history entry are
places the box can show; Up and Down move between them, and an edit changes only the text of the
place shown. Leaving an edited recall with Up or Down keeps its edited copy, shown again on return;
the stored entry never changes. Enter (which sends the box) and Escape (which empties it on
purpose) drop the draft and every edited copy. Up at the oldest entry leaves the box untouched.
History is trimmed of spaces and tabs. This replaces the reference's "Recalled line" and "Unsent
draft" rows wherever they would lose text. Rejected: the reference's rule that recalling always
shows the stored text (an edit, then Up, then Down, lost the edit).
source: agent, on a review finding (an edited recall was lost on Up or Down)

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

RULE These are the only ways the client puts text into the box. Each is a box edit, applied by the
UI loop in arrival order (see Ordering).
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
default: one rule, the row above, for every clicked player name, tell senders included. An empty box
has its caret at the start, so `ui.md` 2.5's case is included.

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
| Macro text starting with `$` | Not a client command and not an alias: the rest of the text is a watchword slot name, spoken as for `$<slot>` (`scope: later`, `later/watchwords.md`). With no watchword slots loaded, nothing is sent and `[watchword] nothing queued for $<slot>` is printed. |
| `$name` anywhere else | Replaced by the session alias `name` (see Expansion). |
| `$VER` | Replaced by the version string. |
| `^1`, `^2`, `^3` | Replaced by the body of that control-macro slot. |
| `$<watchword slot>` | Replaced by the slot's queued answer (`scope: later`, `later/watchwords.md`). |
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
| `say $VER` | `say MuckaII v<version>` | `$VER` is case-sensitive; `$ver` is left alone |
| `say ^1 now` with `^1=look` | `say look now` | bare `^n` expands mid-line |
| `say $$VER` | `say $VER` | doubled sigil is a literal |
| `$$` | `$` | |
| `say $$$VER` | `say $MuckaII v<version>` | pairs collapse left to right, the odd one interpolates |
| `say ^^1` | `say ^1` | |
| `say $nosuchalias` | `say $nosuchalias` | unknown names pass through |
| `^9` mid-line | `^9` | only `^1`-`^3` are slots |
source: reference

open: `$VER` expands to `Mucka v<version>` in the reference. What does MuckaII send?
default: `MuckaII v<version>`, the reference's form with this client's name.
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
default: yes. A typed line `$name rest` runs alias `name`, its first word, with `rest` as the
arguments, as the FACT above defines.
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
| `$?` | exact | `$?` | Reserved: `later/watchwords.md` | later |
| `$<` | exact | `$<` | Reserved: `later/watchwords.md` | later |
| `$con` | exact lowercase | `$con` | Open the raw protocol console: an in-window panel on every platform that shows the wire in both directions and sends raw material into the send stream (see below) | launch |
| `$fkeys` | `fkeys` exact; `fkeys <layer>` any case | `$fkeys [shift\|ctrl]` | Logged. `[fkeys] F1-F12:` (or `Shift+F1-Shift+F12:`, `Ctrl+F1-Ctrl+F12:`), then `  <label><n> = <macro>` for each non-empty slot, where label is `F`, `Shift+F` or `Ctrl+F` and n is 1-12, or `  (none set)`. Unknown layer: `[fkeys] unknown layer '<x>' (use: shift \| ctrl)` (error) | launch |
| `$f<n>` | `f` or `F` then an integer | `$f1` .. `$f36` (absolute slot) | Insert the note `// <macro>` above the live prompt (prompt redrawn below it), in bright green (palette 10); logged. Errors: `[fkey] <n> out of range (1-36)`, `[fkey] <n> is empty` | launch |
| `$SID` | any case | `$SID` | Reserved: `later/diagnostic-commands.md` | later |
| `$CHATTEST` | any case | `$CHATTEST [slow\|stop]` | Reserved: `later/diagnostic-commands.md` | later |
| `$MARK` | any case | `$MARK ...` | Reserved: `later/diagnostic-commands.md` | later |
| `$SCORE` | any case | `$SCORE [days] [name]` | Reserved: `later/score-graph.md` | later |
| `$VER` | exact uppercase | `$VER` | Send the version string as a line: `MuckaII v<version>` (the `$VER` open item, Expansion; the reference sends `Mucka v<version>`) | launch |
| `$map` | - | - | Reserved name with no command; falls to watchword speak | - |
| `$<alias>` | any case | `$<alias>` | Send the alias body, expanded | launch |
| `$<anything else>` | - | `$<slot>` | Watchword speak (`later/watchwords.md`). With no watchword slots loaded, nothing is sent and `[watchword] nothing queued for $<slot>` is printed | launch (the no-slot outcome); later (speak) |
source: reference

open: What does a `scope: later` command in the table do when it is typed before it is built?
default: as `$map` does: the name stays reserved (see Definition rules) and the line falls to
watchword speak, so nothing is sent and `[watchword] nothing queued for $<name>` is printed.

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

FACT In the reference on Android, the `$con` line reads `(Windows only)` and the slot line reads
`^1/^2/^3=command  bind a slot; type ^1 to run it`. In MuckaII the `$con` line is the same on every
platform, because the raw console exists everywhere (see "Raw console").
source: reference, director

## Watchwords (scope: later)

FACT Watchwords (`$?`, `$<`, `$<slot>` speak and substitution): `scope: later`, `later/watchwords.md`.
source: director

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
default: each returns to live, then acts, as `keys.toml` already says for Ctrl+R and Ctrl+W. No key
is swallowed in scrollback.
source: reference

open: Scrollback swallows typed keys, which conflicts with Invariant #1. See `keys.toml`.
default: the first printable key returns to live and lands in the line buffer, and so does every
key after it. No key is ever discarded.
source: reference

EVIDENCE clio scrolls back with PageUp and PageDown, returns to live with Ctrl-A, and keeps a
300-line buffer by default.
source: clio:man/clio.6 SCROLL BACK

## Chat mode

FACT Chat mode (chat-only view filter): `scope: later`, `later/chat-mode.md`.
source: director

## Mobile specifics

RULE On Android the command box differs from desktop as follows.
enforced_by: none: judgement
source: reference

| Aspect | Android behaviour |
|---|---|
| Ctrl+1..3 | Not available on a soft keyboard. Type `^1`, `^2` or `^3` and Enter to run a slot. |
| Hardware keyboard | Only F1-F12 (with Shift and Ctrl layers), Ctrl+D, Up, Down and Escape are bound. Ctrl+Shift+D speaks the plain dreamword. |
| F-keys | The on-screen F-key bar (plain layer) is the main way to send macros. |
| `$con` | The reference prints `[command] $con is Windows only.` MuckaII opens the raw console, as on desktop (DECISION below) |
| Watchwords | `scope: later`: `later/watchwords.md` |
| Scrollback | Finger drag; no PageUp. |
source: reference

DECISION MuckaII's raw console exists on every platform, Android included. Rejected: Mucka's
Windows-only `$con`.
source: director

## Raw console (scope: launch)

FACT The raw console is how the director sees and uses the low-level wire. It is an early
feature, because experiments such as the probe-block comma test (`session.md`) are run from it.
source: director

RULE The raw console shows the wire in both directions, with bytes outside printable 7-bit shown
as escapes (`\e`, `\xNN`). It has its own input line, and text sent from it goes into the send
stream verbatim after the escapes are decoded. It bypasses the command box's 7-bit filter, the
alias and macro expansion, and probe injection. Every send from it is marked in the wire log as a
raw-console send. Its input line is a real text field, so it is an Invariant #0 exception while
it has focus.
source: director
enforced_by: none: judgement

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
