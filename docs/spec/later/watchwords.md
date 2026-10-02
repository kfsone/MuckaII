# Later: watchwords

FACT `scope: later`. Importing the `[watch]` text verbatim is launch (`persistence.md`, Import;
`mucka-ini.md`, `[watch]`), so the rules are waiting when the feature is built. `fair-play.md`
holds an `open:` line asking for a charter review before watchwords are built.
source: director

FACT Watchwords answer puzzles: the player configures a trigger text; `$<` scans recent output
for it and queues an answer under a slot name; the answer is later spoken with `$<slot>` or
substituted into outgoing text. Nothing scans automatically.
source: reference

RULE The rules live in `mucka.ini` and are hand-written; the client never writes them. They are
imported verbatim (`INTERNAL.md`, Data).
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

FACT In the reference on Android, watchwords were effectively unavailable: its ini was app-private,
so nobody could hand-edit `[watch]`.
source: reference
