# Later: score graph

FACT `scope: later`. Until it exists, the Tombstone's name link is inert (`flow.md`, Tombstone) and
the Profile state has no way into it (`flow.md`, Profile). `$SCORE` stays a reserved name in
`input.md`.
source: director

FACT What the user saw of Mucka's score graph:

| Feature | What the user sees | source |
|---------|--------------------|--------|
| Score graph | `$SCORE` / tapping the score opens a graph of score over the session, with a skull U+2620 at a persona wipe | reference |
| Score tap target | Tapping the score (with a magnifier U+1F50D) opens the graph | reference |

RULE `$SCORE` behaves as in the table.
enforced_by: none: judgement
source: reference

| Command | Name match | Syntax | Effect and output |
|---|---|---|---|
| `$SCORE` | any case | `$SCORE [days] [name]`, either order | Open the score graph panel for `name` (default: current character) over `days` (default 3, clamped 1-90). Error: `[score] <reason>; usage: $SCORE [days] [name]` (error) |

FACT Related entries elsewhere: Escape closes the score graph panel (`input.md`, Escape;
`keys.toml` Escape); the panel's size keys `scorepanelwidth` and `scorepanelheight` are in
`mucka-ini.md`; Score is a menu item (`ui.md` section 7).
source: director
