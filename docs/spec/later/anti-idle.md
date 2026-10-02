# Later: anti-idle keepalive

DECISION Anti-idle keepalive is `scope: later`, with keep-screen-on (`later/keep-screen-on.md`).
source: director

| Item | Value | Source |
|------|-------|--------|
| Setting | per profile, seconds of player inactivity; ini key `antiidle` | reference |
| Default | 0 = off | reference |
| Range | 0..3600 | reference |
| Checked | once a second | reference |
| Condition | in game mode, connected, and no player-sent line for at least N s. Probes and telnet replies do not count as player sends. | reference |
| Sent | an empty line: `0D 0A` | reference |
| Related | a "keep screen on" profile switch, default on for mobile, off for desktop | reference |

FACT The keepalive's place in the send order is launch spec: it waits behind any queued line
(`input.md`, Ordering), and its wire-log origin is `keepalive` (`architecture.md`).
source: director
