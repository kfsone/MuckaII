# Session: player state, command understanding, probe injection

FACT MUD2's client support is scrappy. The client has to poll the server for mundane information
such as health, the answers come back poorly presented, and every poll spends a command slot.
source: director

## Sleep (scope: launch)

FACT While the persona is asleep, the probes FES, FEW, FEI, FEX and their kin get no response. If
the client does not track sleep, the status bar goes stale without anyone noticing, and the status
bar is in launch scope.
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

## Command understanding and probe injection (scope: launch)

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

RULE The layers split the work on an outbound line as follows:

| Layer | Does |
|-------|------|
| client | parses and expands the player's input (client `$` and `^` commands, aliases, F-key and `^n` macros) and hands each finished line to the session layer. It never writes to the socket and never inserts a probe |
| session | owns the single ordered outbound queue. It finds the command boundaries in each line it is handed, recognises significant commands (sleep in its variants: `sleep`, `zz`, `zzz`, `zz in bed`, ...), inserts probe blocks at those boundaries, and passes the result to the one socket writer |

The session layer knows which commands take a message argument, because those soak up commas
without quotes, so a comma inside one is not a boundary. Rather than sending probes as separate
lines, it embeds them in commands the player sends.
source: director
enforced_by: none: judgement

FACT MUD2 separates commands with `,` and with `.`.
source: director

open: Which layer finds command boundaries and recognises sleep commands (OPEN H3)? This is
deferred until the proof of concept plays. The candidate design, from two independent agent
brainstorms: the `mud` layer gets a pure, policy-free parse of an outbound line into commands and
separators, and an unsure parse gives no mid-line slot. The outbound line is carried as segments
(typed, separator, probe). A probe can sit only at the start of a line or after a separator, and
stripping the probe segments gives back the client's output. Session raises probe "wants" with a
placement and a deadline, resolved when a line is written. Probes are an optional subsystem
(`architecture.md`, Composition).
default: build no probe injection until this is ruled.

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

