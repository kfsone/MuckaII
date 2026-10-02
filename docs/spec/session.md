# Session: player state, command understanding, probe injection

FACT MUD2's client support is scrappy. The client has to poll the server for mundane information
such as health, the answers come back poorly presented, and every poll spends a command slot.
source: director

## Sleep (scope: launch)

FACT While the persona is asleep, the probes FES, FEW, FEI, FEX and their kin get no response. If
the client does not track sleep, the status bar goes stale without anyone noticing, and the status
bar is in launch scope.
source: director

RULE The session layer tracks a sleep state. Tracking it, and waking, takes a little finesse:

| State | Entered by | Left by | Probes |
|-------|-----------|---------|--------|
| awake | waking (deliberately or not); game entry | falling asleep (any cause) | sent as normal |
| asleep | the player sleeping deliberately; being put to sleep | waking | held, not sent |
| drugged | a drugged sleep, for example from eating the wafer with poppy seeds | waking | held, not sent |

source: director
enforced_by: none: judgement

FACT The wire corpus in `~/.mucka/mucka.db` is large, but it probably misses a few ways of falling
asleep and waking. The drugged sleep from the poppy-seed wafer was first seen on 2026-09-30.
source: director

open: The exact received text for each way into and out of sleep. It is mined from the wire
corpus, and every pattern carries the capture it came from.

RULE Mining the wire corpus for patterns follows these limits:
- Query only the `wire` table, and only received records (`direction = 0`).
- Never print sent records, because they hold login credentials.
- Write the patterns into the spec, never other players' words.
- Only `wire` is evidence. Mucka's derived tables (encounters, fights and the like) are reference
  output.

source: director
enforced_by: none: judgement

## Command understanding and probe injection (scope: launch)

RULE After the persona has finished arriving in the tea room, MuckaII never does anything that
affects gameplay automatically. Every gameplay action needs a manual input. Before arrival
(login and persona selection) the client may drive the shell itself.
source: director
enforced_by: none: judgement

RULE The client injects only read-only probes (FES, FEW, FEI, FEX), and never a command that
changes game state.
source: director
enforced_by: none: judgement

FACT Probes are not free. MUD2 allows each user one command action per server tick: the `n` out of
`n,sw,e`. A probe block placed between two moves (`n,ESC-[FES,FEW,FEI,FEX ESC-]s`) delays the
second move by a fraction of a second, which is well within human cognition. The client does not
model the tick length.
source: director

RULE The client layer parses the player's input well enough to find the command boundaries and
recognise significant commands (sleep in its variants: `sleep`, `zz`, `zzz`, `zz in bed`, ...).
It knows which commands take a message argument, because those soak up commas without quotes, so a
comma inside one is not a boundary. Rather than sending probes as separate lines, it embeds them in
commands the player sends.
source: director
enforced_by: none: judgement

RULE Injection sites around sleep:

| Situation | What the client does |
|-----------|----------------------|
| The player's line ends in a sleep command | Put the probe block *before* the sleep command, at its command boundary: `u,u,u,u,se,invis,qw,ESC-[fes,few ESC-]zz in bed`. This delays the sleep by a couple of server ticks, which is acceptable. A probe placed *after* the sleep command is parsed once the persona is asleep and gets no answer. |
| The player sleeps and is woken within about 100 ms | No probe is needed. |
| A probe gets no response, so the persona is asleep | When the player wakes, send a probe within 1 s. |
| A wake message arrives and the player at once sends a command (for example `zz` again, fighting the sleep system) | That line is the injection site: `ESC-[fes,few ESC-]zz`. |

FACT The scenario that matters most: a player has just been beaten and wants to recover
stamina. They are very vulnerable to PvP, so up-to-date stats and FEW are especially valuable. They
run from the Cellar to the Small Bedroom with `u,u,u,u,se,invis,qw,make bed,zz in bed`. From then
until they wake, FES, FEI, FEX and FEW get nothing back, and only non-client commands work, which
wake the player.
source: director

EVIDENCE The director tested this on mud2.com on 2026-10-01. With a comma before the block,
`make bed,$-[few$-]zz in bed` worked properly. Without the comma, `make bed$-[few$-]zz in bed` was
parsed as `make bedzz in bed`, which MUD2's parser rejected, and the FEW output was wedged onto the
end of the echo line. The session (`$-` is the director's notation, and `\e` is ESC):

```
sent> make bed\e-[few\e-]zz in bed
*wake,make bed[persona] the champion
*zz in bed
I don't know the word "bedzz".
```

The persona's name is replaced with `[persona]`. Everything else is verbatim.
source: director

FACT The server lifts an escaped block out of the command text, and the text on either side of the
block joins up. A block needs no separator of its own after `ESC-]`, but the command boundary in
front of it must still be there.
source: director (the test above)

RULE An injected probe block goes either at the very start of the line or directly after an
existing comma, and never directly after command text. Nothing is added after its closing `ESC-]`.
source: director
enforced_by: none: judgement

RULE Injection and Invariant #1:
- The player's commands reach the socket unmodified and in their typed order.
- An injected probe block goes only at a command boundary: before the first command of a line,
  between two of its commands, or between two lines. It never goes inside a command.
- Every injection is marked in the wire log with its position.
- The acceptance test removes the marked probe blocks and compares what is left with the typed
  stream.

source: director
enforced_by: none: judgement

