# Later: diagnostic `$` commands (`$SID`, `$MARK`, `$CHATTEST`)

FACT The names stay reserved in `input.md`'s `$` command table and its Definition rules. Output
colours and the "logged" marking follow `input.md`, `$` commands.
source: director

RULE These commands behave as in the table.
enforced_by: none: judgement
source: reference

| Command | Name match | Syntax | Effect and output |
|---|---|---|---|
| `$SID` | any case | `$SID` | `[sid] s=<host>/m=<client run id>/p=<persona>[@<reset number>]/i=<login id>`; each unknown part reads `unknown` |
| `$CHATTEST` | any case | `$CHATTEST [slow\|stop]` | Play a scripted chat into the terminal as the current persona; sends nothing, stays out of the transcript, and never sets the Ctrl+R teller. `slow`: one script line at a time, each after 0.5 s plus an exponential delay of mean 1 s, capped at 9 s; notes `// $CHATTEST slow: ...` on start, finish and stop. `stop`: end a slow run, or `// $CHATTEST: nothing is running`. Needs a persona: `[chattest] needs a persona in the game - the script speaks as you` (error). Bad argument: `[chattest] unknown argument '<x>'; usage: $CHATTEST [slow\|stop]` (error). A slow run stops on game-mode exit and disconnect. |
| `$MARK` | any case | `$MARK [@id\|"note"]`, `$MARK start [@id\|"note"]`, `$MARK end [@id\|"note"]` | Logged landmark. Bare: `//MARK: <id>`. `start`: `//BEG: <id>`, opens a span; spans nest to depth 8. `end`: `//END: <id>`, closes the innermost (with an id it must match the innermost). Ids: `mm-<login id>:<id>` when supplied, `mm-<login id>-<8 hex>` when generated. `@id` is a letter or digit then letters, digits, `.`, `_`, `-`. A note is quoted, 1-64 characters, no inner quote, nothing after it. On login end every open span is closed as `//END: <id> (<reason>)`, default reason `session ended`. Errors start `ERROR: ` and are not logged: needs a login (`$MARK needs an open persona session - you are not in game mode.`), `Too many open marks (maximum 8).`, `No open marks to close.`, `<id> is not the current mark (<current>)` (plus a hint to use `$MARK end` when the current one was generated), unterminated, trailing text, empty or over-long note, `Expected @id or a "quoted note", got: <x>` |

open: Scope of `$SID`, `$MARK` and `$CHATTEST`. They serve the wire log (launch) but are
diagnostics.
default: later.
