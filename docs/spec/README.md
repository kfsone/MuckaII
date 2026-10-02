# docs/spec

FACT This directory says what MuckaII must do, as behaviour that can be observed from outside the
program. It is the only form in which the previous client (Mucka) reaches this project.
source: director

## Source tags

Every statement ends with a `source:` line. The sources, strongest first (`CLAUDE.md`, Evidence):

- `bartle:FE4 <section or code>`: `G:\Source\mud-fe\mud2_FE4.txt`
- `bartle:FECodes <section>`: `G:\Source\mud-fe\MUD-FECodes.txt`
- `capture:<file>.c1`: `docs/evidence/captures/`
- `clio:<relative path>`: `G:\Source\clio-1.8a`
- `director`: a rule the director set
- `reference`: what Mucka observably does. This is the weakest source. It is a behaviour to keep
  unless evidence says otherwise, and it is never a claim about how the server works.

Statement tags are the same as in the root `CLAUDE.md`: FACT, RULE, DECISION, EVIDENCE.

## What a spec line may not contain

RULE No names from the reference implementation: no type, file, method, namespace, setting
property, or test name. Names that belong to the outside world are fine: `mucka.ini` section and key
names (they are user data), protocol code names (C98, FES), telnet option names, key chords, `$`
commands.
source: director
enforced_by: scripts/check-tripwire.py (for the listed names); otherwise none: judgement

RULE A workaround for MAUI, WinUI, or Android is not a requirement. Record the user-visible need it
served, if there was one, and drop the mechanism.
source: director
enforced_by: none: judgement

RULE Mark each feature `scope: launch` or `scope: later`. The launch scope is in `INTERNAL.md`.
source: director
enforced_by: none: judgement

RULE Tables over prose. Where a list of codes, keys, or fields exists, it is a TOML or Markdown
table, not a paragraph.
source: director
enforced_by: none: judgement

RULE Where sources disagree, record both and apply the evidence ranking in `CLAUDE.md`. If the
ranking decides, record a DECISION that names the deciding source. If it does not, add an `open:`
line with a `default:`. Never silently pick one.
source: director
enforced_by: none: judgement

RULE Every `open:` line is followed by a `default:` line saying what to build until the director
rules. An `open:` with a `default:` can be implemented. One without is a question for the director
before any code is written.
source: director
enforced_by: scripts/check-spec-tags.py (report-only until every default is written)

RULE Spec files contain only 7-bit characters. This is a rule about the files. The wire format is
MUD2's own markup encoding and is described in `protocol.md`.
source: director
enforced_by: scripts/check-text-files.py

## Which specs each layer answers to

| Layer or part | Read |
|---------------|------|
| everything | `CLAUDE.md`, `fair-play.md`, `architecture.md` |
| mud | `protocol.md`, `c1-codes.toml`, `events.md`, `docs/evidence/golden/README.md`, `docs/evidence/captures/README.md` |
| transport | `connection.md` sections 1-2 |
| session | `connection.md` sections 3-5, `session.md`, `flow.md` |
| client | `input.md`, `keys.toml` |
| GUI | `ui.md`, `sounds.toml`, `flow.md`, `input.md` |
| persistence and importer | `persistence.md`, `mucka-ini.md` |
| build | `docs/build.md` |

## Files

| File | Owns |
|------|------|
| `architecture.md` | Layers, threads, event delivery, the outbound queue, the bot |
| `fair-play.md` | What features may and may not do. Outranks every other spec |
| `flow.md` | The states: profile, pre-game, game, tombstone. Also settings behaviour |
| `session.md` | Sleep state, command understanding, probe injection |
| `protocol.md`, `c1-codes.toml` | Byte stream: C1 sequences, ANSI subset, encoding, line and prompt model |
| `events.md` | The neutral event schema that golden streams are written in |
| `connection.md` | Telnet options, login sequence, client-mode entry, probes, keepalive |
| `input.md`, `keys.toml` | Command box, history, F-keys, hotkeys, aliases, `$` commands, scrollback, raw console |
| `ui.md`, `sounds.toml` | Status bar, dreamword, terminal look, sounds and their triggers |
| `persistence.md`, `mucka-ini.md` | `~/.muckaii/`, the wire log, settings storage, and the `mucka.ini` import format |
| `OPEN.md` | Every `open:` item above, deduplicated and grouped for one ruling pass |
| `later/` | Specs for features outside the launch scope. Never built until the director triages them |
