# Later: watchwords

FACT `scope: later`. Importing the `[watch]` text verbatim is `scope: now` (`persistence.md`, Import).
`fair-play.md` holds an `open:` asking for a charter review before watchwords are built.
source: director

FACT Watchwords answer puzzles: the player configures a trigger text; `$<` scans recent output
for it and queues an answer under a slot name; the answer is later spoken with `$<slot>` or
substituted into outgoing text. Nothing scans automatically.
source: reference

RULE The rules live in `mucka.ini`, hand-written; the client never writes them. Their grammar,
matching and limits are `mucka-ini.md`, `[watch]` and `[watch slotname]`.
enforced_by: none: judgement
source: reference, director

## Commands

RULE The watchword commands behave as in the table. Client output lines are in the info colour
(`input.md`, `$` commands).
enforced_by: none: judgement
source: reference

| Command | Name match | Syntax | Effect and output |
|---|---|---|---|
| `$?` | exact | `$?` | `[watchword] $a  $b ...` (slot names, two spaces apart) or `[watchword] no slots loaded` |
| `$<` | exact | `$<` | Scan the last 80 committed terminal lines (blank lines skipped, joined by spaces) for watchword triggers; per match `[watchword] queued "<answer>" -> $<slot>`; none: `[watchword] no matches in recent history` |
| `$<anything else>` | - | `$<slot>` | Watchword speak: send `"<answer>` (a leading quote, MUD2's say) and clear the slot; nothing queued: `[watchword] nothing queued for $<slot>` |

FACT Watchword `$<slot>` references in outgoing text are expansion pass (2), between aliases and the
`$$` collapse (`input.md`, Expansion of outgoing text). An F-key macro whose text starts with `$`
speaks that slot (`input.md`, Macro text syntax).
source: reference

## The `[watch]` rules

RULE Each slot queues one answer; speaking or substituting it clears it. Section and slot names are
case-insensitive. Everything else about the rules is `mucka-ini.md`, `[watch]`.
enforced_by: none: judgement
source: reference
