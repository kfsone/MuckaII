# Session: player state, command understanding, probe injection

FACT MUD2's client support is scrappy. The client has to poll the server for mundane information
such as health, the answers come back poorly presented, and every poll spends a command slot.
source: director

## Sleep (scope: now)

FACT While the persona is asleep, the probes FES, FEW, FEI, FEX and their kin get no response. If
the client does not track sleep, the status bar goes stale without anyone noticing, and the status
bar is `scope: now`.
source: director

RULE The session layer tracks a sleep state:

| State | Entered by | Left by | Probes |
|-------|-----------|---------|--------|
| awake | waking (deliberately or not); game entry | falling asleep (any cause) | sent as normal |
| asleep | the player sleeping deliberately; being put to sleep | waking | held, not sent |
| drugged | a drugged sleep, for example from eating the wafer with poppy seeds | waking | held, not sent |

source: director
enforced_by: none: judgement

FACT Eating the wafer with poppy seeds puts the persona into a drugged sleep.
source: director

open: The exact received text for each way into and out of sleep. It is mined from the wire
corpus, and every pattern carries the capture it came from.
default: until the patterns are mined, `asleep` is entered when the player's line ends in a sleep
command, or when an FES-carrying probe goes unanswered for more than 5 s (`connection.md` 5.2). The
state returns to `awake` when the player sends a line that is not a sleep command, and the probe in
the injection table below follows within 1 s. `drugged` is not told apart from `asleep`.

RULE Mining the wire corpus for patterns follows these limits:
- Query only the `wire` table, and only received records (`direction = 0`).
- Never print sent records, because they hold login credentials.
- Write the patterns into the spec, never other players' words.
- Only `wire` is evidence. Mucka's derived tables (encounters, fights and the like) are reference
  output.

source: director
enforced_by: none: judgement

## Command understanding and probe injection (scope: now)

Nothing gameplay-affecting happens automatically after arrival, and injected commands are
read-only probes only: `fair-play.md`, Automation. The setup batch is part of arrival:
`connection.md` 5.3.

RULE Before arrival (login and persona selection) the client may drive the shell itself.
source: director
enforced_by: none: judgement

FACT `$-` is `ESC -` (`connection.md`). A probe block has no spaces: `$-[fes,few$-]` is exactly
`1B 2D 5B 66 65 73 2C 66 65 77 1B 2D 5D`.
source: director; bartle:FE4 2)

FACT Probes are not free. MUD2 allows each user one command action per server tick: the `n` out of
`n,sw,e`. A probe block placed between two moves (`n,$-[FES,FEW,FEI,FEX$-]s`) delays the second
move by a fraction of a second, which is well within human cognition. The client does not model
the tick length.
source: director

FACT MUD2 separates commands with `,` and with `.`.
source: director

## Outbound path

DECISION A line goes out through these stages, in this order:

| Stage | Owner | Triggered by | May do |
|-------|-------|--------------|--------|
| 1. Expansion | optional modules attached by the builder (variables, macros, interpolation, aliases with named positional arguments) | the player | rewrite the line freely. `$hunt thief north axe` becomes `g cc,go north,drop cc,kill thief with axe`. The output is what the player sent: the Invariant #1 baseline |
| 2. Parse | `mud` | every line | split the line into commands and separators per MUD2's own grammar: separators `,` and `.`, verbs that take a message (these absorb separators), and sleep variants. A pure parse that holds no policy. Where the parse is unsure, the line gets no mid-line slot |
| 3. Intercepts | modules registered with the builder, run in registration order; probe injection is one | the client, automatically | insert marked segments at line-start or separator slots only. An intercept never alters a command the player sent |
| 4. Queue and write | `session`, then `transport`'s single writer | every line | send the segments in order. The wire log marks each inserted segment |

source: director

DECISION The prepared line is carried as segments: typed, separator, and inserted. Removing the
inserted segments gives back, byte for byte, the line that expansion produced. That is how
Invariant #1 is checked.
source: director

DECISION A raw send (the raw console, or a consumer's explicit raw call) skips expansion, parsing
and intercepts. It goes straight to the queue and is marked `raw` in the wire log. Raw sends are
the exception; consumers normally hand over a line to be prepared.
source: director

DECISION `mud` holds MUD2's command grammar and nothing else. Variables, macros, interpolation and
aliases are never part of `mud`. They are added on top as optional modules, so a bare consumer
(an analysis tool, a bot) gets plain MUD2 parsing.
source: director

RULE Macros whose expansion ends in an attack (like `$hunt`) last for the session only:
`fair-play.md`, Automation.
source: director
enforced_by: none: judgement

DECISION Probe injection is an intercept. It works from session state (sleep, wakes, unanswered
probes): it raises a probe "want" with a placement (before a sleep command, or at the next
separator) and a deadline. The want is resolved when a line is written. Wants for the same field
merge. A want whose deadline passes with no line to carry it goes out as its own line, and a stale
want expires.
source: agent; director

RULE Injection sites around sleep:

| Situation | What the session layer does |
|-----------|-----------------------------|
| The player's line ends in a sleep command | Put the probe block *before* the sleep command, at its command boundary: `u,u,u,u,se,invis,qw,$-[fes,few$-]zz in bed`. This delays the sleep by a couple of server ticks, which is acceptable. A probe placed *after* the sleep command is parsed once the persona is asleep and gets no answer. |
| The player sleeps and is woken within about 100 ms | No probe is needed. |
| A probe gets no response, so the persona is asleep | When the player wakes, send a probe within 1 s. |
| A wake message arrives and the player at once sends a command (for example `zz` again, fighting the sleep system) | That line is the injection site: `$-[fes,few$-]zz`. |

source: director
enforced_by: none: judgement

FACT The scenario that matters most: a player has just been beaten and wants to recover
stamina. They are very vulnerable to PvP, so up-to-date stats and FEW are especially valuable. They
run from the Cellar to the Small Bedroom with `u,u,u,u,se,invis,qw,make bed,zz in bed`. From then
until they wake, FES, FEI, FEX and FEW get nothing back, and only non-client commands work, which
wake the player.
source: director

EVIDENCE The director tested this on mud2.com on 2026-10-01, sending raw material into the send
stream from Mucka's `$con` raw console, not from the command box. With a comma before the block,
`make bed,$-[few$-]zz in bed` worked properly. Without the comma, `make bed$-[few$-]zz in bed` was
parsed as `make bedzz in bed`, which MUD2's parser rejected, and the FEW output was wedged onto the
end of the echo line. The session (`\e` is ESC):

```
sent> make bed\e-[few\e-]zz in bed
*wake,make bedOllie the champion
*zz in bed
I don't know the word "bedzz".
```
source: director

FACT The server lifts an escaped block out of the command text, and the text on either side of the
block joins up. A block needs no separator of its own after `ESC-]`, but the command boundary in
front of it must still be there.
source: director (the test above)

RULE An injected probe block goes either at the very start of the line or directly after an
existing comma, and never directly after command text. Nothing is added after its closing `ESC-]`.
source: director
enforced_by: none: judgement

RULE Injection under Invariant #1 (root `CLAUDE.md`, checkpoint 2): a probe block goes only at a
command boundary (before the first command of a line, between two of its commands, or between two
lines), never inside a command, and each one is marked in the wire log with its position.
source: director
enforced_by: none: judgement

