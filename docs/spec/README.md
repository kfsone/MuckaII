# docs/spec

FACT This directory is what MuckaII must do, as behaviour observable from outside the program. It
is the only form in which the previous client (Mucka) reaches this project.
source: director

## Source tags

Every statement ends with a `source:` line. Statement tags are those of the root `CLAUDE.md`. Sources,
strongest first (ranking: `CLAUDE.md`, Evidence):

| Token | Means |
|-------|-------|
| `bartle:FE4 <section or code>` | `G:\Source\mud-fe\mud2_FE4.txt` |
| `bartle:FECodes <section>` | `G:\Source\mud-fe\MUD-FECodes.txt` |
| `capture:<file>.c1` | `docs/evidence/captures/` |
| `clio:<relative path>` | `G:\Source\clio-1.8a` |
| `director` | a rule or ruling the director set |
| `agent` | an agent's design choice, inference or count, not ruled by the director. Marks authorship, not evidence: it never settles server behaviour, and the director may overturn it without new evidence |
| `reference` | what Mucka observably does. Weakest: a behaviour to keep unless evidence says otherwise, never a claim about the server |

## What a spec line may not contain

RULE No names from the reference implementation: no type, file, method, namespace, setting
property, or test name. Outside-world names are fine: `mucka.ini` sections and keys (user data),
protocol code names (C98, FES), telnet option names, key chords, `$` commands.
source: director
enforced_by: scripts/check-tripwire.py (for the listed names); otherwise none: judgement

RULE A workaround for MAUI, WinUI, or Android is not a requirement. Record the user-visible need it
served, if any, and drop the mechanism.
source: director
enforced_by: none: judgement

RULE Mark each feature `scope: now` or `scope: later`. The now scope is listed in `INTERNAL.md`,
Scope; a spec's tag follows that list.
source: director
enforced_by: none: judgement

RULE Tables over prose. A list of codes, keys, or fields is a TOML or Markdown table.
source: director
enforced_by: none: judgement

RULE Where sources disagree, record both and apply the evidence ranking (`CLAUDE.md`). If it
decides, record a DECISION naming the deciding source. If not, add an `open:` line with a
`default:`. Never silently pick one.
source: director
enforced_by: none: judgement

RULE Every `open:` line is followed, in the same paragraph, by a `default:` line: what to build
until the director rules. An `open:` with a `default:` can be implemented; one without blocks code.
source: director
enforced_by: scripts/check-spec-tags.py

Spec files are 7-bit text (`CLAUDE.md`, Comments and docs); the wire encoding is in `protocol.md`.

## Which specs each layer answers to

| Layer or part | Read |
|---------------|------|
| everything | `CLAUDE.md`, `fair-play.md`, `architecture.md`, `docs/style.md` |
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
| `later/` | Specs for features outside the now scope. Never built until the director triages them |
