# docs/spec/later

FACT These files hold the specs of features outside the launch scope (`INTERNAL.md`, Scope). None of
them is built until the director triages it against `fair-play.md`. The launch specs keep a one-line
row for each, with `scope: later` and a pointer here.
source: director

| File | Feature | Pointed to from |
|------|---------|-----------------|
| `anti-idle.md` | Anti-idle keepalive | `connection.md` 5.5 |
| `chat-mode.md` | Chat-only view filter | `input.md` Chat mode; `ui.md` 8 |
| `combat-panel.md` | Combat panel, combat rail, metronome, combat edge, combat status icon | `ui.md` 0, 3.4, 8 |
| `compass.md` | Radar compass | `ui.md` 8 |
| `diagnostic-commands.md` | `$SID`, `$MARK`, `$CHATTEST` | `input.md` `$` commands |
| `drop-overlay.md` | Drop headline overlay, relogin after a Reset drop | `connection.md` 1.2; `ui.md` 8 |
| `keep-screen-on.md` | Keep screen on | `connection.md` 5.5; `ui.md` 6, 7, 8 |
| `lastsaw-fetch.md` | `$LASTSAW`, `$FETCH` | `INTERNAL.md` Scope |
| `persona-deletion.md` | Persona deletion | `flow.md` |
| `probe-cadence.md` | Probe cadence rows for later features | `connection.md` 5.2 |
| `reformatters.md` | Output reformatters | `INTERNAL.md` Scope |
| `score-graph.md` | Score graph, `$SCORE`, score tap target | `flow.md`; `input.md` `$` commands; `ui.md` 8 |
| `session-transcript.md` | Session transcript (`rec`), recording chip | `persistence.md`; `ui.md` 3.2, 3.5, 8 |
| `setting-default-change.md` | Telling the player a setting's default changed | `flow.md` |
| `side-panel.md` | Side panel, floating widgets, players list | `flow.md` Game; `ui.md` 0, 8 |
| `watchwords.md` | Watchwords | `input.md`; `fair-play.md`; `ui.md` 8 |

source: director

FACT Some later items stay where they are, because they are rows in launch data tables: `keys.toml`
Ctrl+W and Ctrl+`, `sounds.toml` `metronome-book-ends` (named by `architecture.md`) and the
percussion rows, the `scope = "later"` codes in `c1-codes.toml`, the later keys in `mucka-ini.md`,
and the later FE reply facts in `protocol.md`.
source: director
