# Persistence

FACT This file says what MuckaII keeps on disk and what the user can see and query there: the data
directory, the wire log, the session transcript, the schema rules, and the one-time import of
Mucka's `mucka.ini`. The ini format itself is in `mucka-ini.md`.
source: director

| Feature | Scope |
|---------|-------|
| Data directory | `scope: launch` |
| Wire log | `scope: launch` |
| Schema forward-compatibility | `scope: launch` |
| First-run import of `mucka.ini` | `scope: launch` |
| Session transcript (`rec`) | `scope: later` |

source: director

open: the session transcript is not in the launch scope list in `INTERNAL.md`. Confirm it is later.

## Data directory

DECISION Runtime data lives in `~/.muckaii/`.
source: director

| Platform | Directory |
|----------|-----------|
| Windows | `%USERPROFILE%\.muckaii\` |
| Linux, macOS | `$HOME/.muckaii/` |
| Android | the app's private files directory. Android has no home directory |

source: director, reference (the Android row is inferred from Mucka's Android behaviour)

RULE On Android, data goes in the app's private files directory, never its cache directory. Android
deletes a cache directory under storage pressure without asking or telling the app, and the user's
"Clear cache" button empties it. The wire log cannot survive that.
enforced_by: none: judgement
source: reference

| Contents | What it holds |
|----------|---------------|
| one SQLite database | the wire log, and any later store that joins it |
| the settings file | MuckaII's own settings, profiles, F-keys and the imported `[watch]` text, in MuckaII's own format |
| the import marker | written once when the `mucka.ini` import is finished (see "Import") |
| session transcripts | `scope: later`. One text file per recording (see "Session transcript") |

open: proposed contents. Only the SQLite wire log and the import are director rulings
(`INTERNAL.md`). The settings file and the marker follow from them.

open: file names inside `~/.muckaii/` (database, settings file, marker, transcript location) are
not yet ruled.

FACT Mucka kept its data in `~/.mucka/` on Windows (one database, `mucka.db`) and in the app's
private files directory on Android. MuckaII does not read, import or modify `mucka.db`.
source: reference, director

## Wire log

RULE The wire log is always on. There is no setting to turn it off, nothing to arm, and no UI to
enable it.
enforced_by: none: judgement
source: director

RULE Every byte in both directions is recorded verbatim, as it came off or went onto the socket.
That includes telnet negotiation, C1 sequences, ANSI and the login exchange. Nothing is decoded,
filtered, compressed or reframed before it is stored.
enforced_by: none: judgement
source: director

RULE The log opens with the connection, before the first byte, so the login exchange is in it like
everything else.
enforced_by: none: judgement
source: reference

RULE Recording a record must never stall typing or the screen (root `CLAUDE.md`, Invariant #1). The
socket and UI threads only hand a record over. One background writer does all the disk work.
enforced_by: none: judgement
source: reference

FACT The director runs two clients side by side, against one data directory and one database.
Global row numbers from two clients interleave, so the per-run sequence is the integrity signal.
source: reference

RULE Two instances sharing one data directory both log in full. Neither blocks the other, and
neither run's sequence has a gap.
enforced_by: none: judgement
source: reference

open: two instances started together on first run can both see the import marker missing. Which
one imports?

### What a record is

| Field | Meaning |
|-------|---------|
| run | which client connection the record belongs to (see the run record below) |
| sequence | 0-based within the run, gapless, in the order records were handed over. A gap means a lost record |
| timestamp | unix milliseconds UTC, read when the record was handed to the log |
| direction | `0` received from the server, `1` sent to the server, `2` client annotation |
| data | the bytes and nothing else: no magic, no length prefix, no embedded timestamp |

source: reference

| Direction | One record is | Data encoding |
|-----------|---------------|---------------|
| `0` received | one socket read, whatever its length | raw bytes. Decoding as Latin-1 is the identity map |
| `1` sent | one socket write | raw bytes. 7-bit ASCII once past the send boundary |
| `2` annotation | one client-side note | UTF-8 text |

source: reference

RULE Sequence order, timestamp order and storage order agree for every record in a run. They are
taken at one serialisation point, so a record never carries an earlier sequence number with a later
timestamp.
enforced_by: none: judgement
source: reference

RULE The direction numbers are stored values and are never renumbered.
enforced_by: none: judgement
source: reference

FACT Read boundaries are TCP segmentation, not protocol structure. A replay must not depend on
where one received record ends and the next begins.
source: reference

| Run record field | Meaning |
|------------------|---------|
| started | unix ms UTC, when the log opened for this connection |
| ended | unix ms UTC; empty means the client died before closing it |
| host | the server this run was against |
| client version | the MuckaII version that wrote it, so a decoder can tell which client wrote the bytes |

source: reference

| Annotations Mucka wrote | Text |
|-------------------------|------|
| dreamword seen | `dreamword detected: <word>` |
| dreamword gone | `dreamword cleared` |
| width confirmed by the server | `terminal width confirmed: <n>` |
| the client's reply to a `$` command | the reply line as shown on screen |
| reset-timer diagnostics (debug, `logttr`) | `reset! <note>` |
| dreamword spoken (Ctrl+D, Ctrl+Shift+D, pill) | `dreamword spoken: <word>` or `dreamword spoken: <word> ,<follow>` (`keys.toml`, `ui.md` 4.2) |
| alternate weapon wielded (Ctrl+W) | `alt weapon: wield <noun>` (`keys.toml`) |

source: reference

open: which annotations MuckaII writes. Annotations are a log of client decisions, not wire
traffic.

### What the user can do with it

FACT The database is a plain SQLite file. The user can open it with any SQLite tool while the client
runs, and one query returns the traffic of a run in order, with timestamp, direction and bytes. The
data column of a record prints as the line itself.
source: reference

FACT MuckaII's table and column names are its own. The fields above are the requirement, not a
schema.
source: director

| Failure | Behaviour |
|---------|-----------|
| The database cannot be opened | the connection still goes ahead. The failure is reported once, in the terminal, and no records are kept for that run |
| A write fails during a run | the log stops for the rest of the run, says so once in the terminal, and stops accepting records. No retry loop and no error line per record |
| The client is closed | queued records are flushed before exit, with a bounded wait |

source: reference

FACT Nothing prunes, rotates or consolidates the wire log. The user reclaims space by deleting rows
by hand.
source: reference

FACT Measured on Mucka over 13 sessions, 18.11 play-hours: 119,921 records, 10,141,176 bytes of
payload, 0.156 KB/s average. Received 76,633 records (9,668,270 bytes), sent 42,946 records
(465,243 bytes, about 11 bytes per typed command), 342 annotations. Stored one row per record with no
indexes, about 0.74 MB per play-hour.
source: reference

FACT In Mucka's wire log the password record is stored in clear in the sent direction, like
every other sent byte. The server does not echo it, so it is absent from the received direction and
from the screen.
source: reference

open: "every byte, verbatim" puts the login password in the wire log in clear, while passwords
otherwise live only in OS secure storage. Director to rule: keep verbatim, or mask the password
record.

## Schema forward-compatibility

FACT MuckaII is released on GitHub. Its databases live on strangers' machines that nobody here can
inspect or repair.
source: director

RULE Every schema change is a numbered, forward-only step. The database records which steps have
run, and each step runs once. A step that has been released is never edited.
enforced_by: none: judgement
source: reference

RULE No destructive migration. A schema step never drops a table or column holding user data and
never deletes rows. Changes are additive.
enforced_by: none: judgement
source: director

RULE The schema is never converged on whatever shape the file happens to have. A rule
re-evaluated at every open keeps acting on a stranger's file, unprompted and with no backup.
enforced_by: none: judgement
source: reference

FACT The reference's schema history includes a step that deleted rows. The no-destructive-migration
rule above is stricter than the reference.
source: reference

open: what a client does with a database written by a NEWER MuckaII (unknown later steps recorded).
Read-only, refuse, or carry on.

## Retention and deduplication

FACT The wire log builds up a lot of redundant bloat as the player walks around. Seeing the same
long room descriptions over and over is one example.
source: director

DECISION Wire payloads are stored content-addressed from the first release. One table maps a hash
to the bytes, and each wire row references a payload by its hash, so repeated text is stored once.
This has to be in the first schema, because a later migration may not rewrite a stranger's
database destructively. Retention policies and compaction come later and work on top of this
table. Rejected: storing each payload inline per row (Mucka's layout).
source: director

open: The retention policy. What ages out, after how long, and whether old payloads are
compressed.

## Settings storage

DECISION A settings store holds only the keys the user has changed. A missing key means "use the
default". This is what lets the UI show that a setting is at its default, and it is what will let
a later release say "this setting's default changed: keep or switch?" (`flow.md`, Settings).
source: director

DECISION Per-profile settings layer on top of the global settings, one key at a time. A key a
profile does not set falls through to the global value, and from there to the default. The
profile page can edit them without logging in. Rejected: a per-profile section that replaces the
global settings wholesale (Mucka's semantics, see `mucka-ini.md` "How the sections combine").
source: director

RULE The importer keeps each Mucka profile's *effective* values. Mucka replaces wholesale while
MuckaII layers, so for a profile that has a `[settings:Name]` section, the importer writes a
profile-level value for every key whose Mucka value differs from what MuckaII's layering would
otherwise produce.
source: director
enforced_by: none: judgement

## Session transcript (`rec`)

`scope: later`

FACT The transcript is the lossy, human-readable half of the pair. It records the screen, not the
socket. The wire log is the byte-exact half. Where the two disagree, the transcript is wrong.
source: reference

| Behaviour | Rule |
|-----------|------|
| Arming | by hand only, never always on. One toggle, available at all times: at the shell, before login, mid-fight |
| Controls | a `rec` chip in the status bar, a compact `(R)` marker (grey idle, red recording), a checkable menu item, and a command-line switch |
| Command-line switch | arms, never toggles. It fires on first game-mode entry and does nothing if recording is already on |
| One file per recording | starting a new recording opens a new file |
| File name | `session-rec.{host}.{yyyyMMdd-HHmmss}.txt`, local time when recording started |
| Host in the name | every character illegal in a file name becomes `_`. A blank host becomes `unknown` |
| Location | the data directory |
| Encoding | UTF-8, no byte-order mark |
| Header | `Recording by Mucka from {host} starting on {yyyy-MM-dd HH:mm:ss}.` then one blank line |
| Footer | one blank line, then `Recording by Mucka from {host} finished on {yyyy-MM-dd HH:mm:ss}.` |
| Content | each screen line as it is committed, as plain text with markup resolved, sanitised and tab-expanded the way the screen shows it |
| Prompt | a prompt that is replaced in place is written once, merged with its echoed command, as the screen shows it |
| Commands | not written separately. MUD2 echoes commands, so the screen already carries them after alias and F-key expansion |
| F-key note | the on-screen `// <macro>` note is written as a standalone line above the prompt it was raised against |
| Client system lines | written, since they are on screen |
| Clear screen | written as `[screen cleared]`. The file never clears. A prompt live at the clear is written first |
| Recording started/stopped/failed notes | shown on screen, never written to the file |
| On stop | a live, uncommitted prompt is written before the footer |
| Durability | every line reaches disk as it is written, so a crash keeps everything up to that point |
| Write failure | ends the recording and says so in the terminal |
| Account ID | not masked. Armed before login, the file holds the echoed account ID. The password is never echoed, so it never appears |
| Chat filter | does not apply. The transcript is the whole stream, recorded once |
| Retention | nothing deletes old transcripts |

source: reference

FACT A reference wire-log record pair showing the echo: sent `wave\r\r\n`, then received
`wave\r\r\n`.
source: reference

open: The pair above ends CR CR LF, while `connection.md` gives the outbound terminator as `0D 0A`
and the captures show the echo ending `\r\n` (`protocol.md`, Echo). Which does the server receive?

FACT File name examples: host `mud2.co.uk` at 2026-09-14 20:30:15 gives
`session-rec.mud2.co.uk.20260914-203015.txt`. Host `a/b` gives `session-rec.a_b.20260914-203015.txt`.
A blank host gives `session-rec.unknown.20260914-203015.txt`.
source: reference

open: header and footer wording for MuckaII (the reference names "Mucka"), and whether the file
name keeps the `session-rec.` prefix.

## Import of `mucka.ini`

`scope: launch`

RULE The import runs once per MuckaII install, on first run. When it finishes, MuckaII writes the
import marker in the data directory. While the marker exists, the import is not offered again.
enforced_by: none: judgement
source: director

RULE The source file is opened read-only. MuckaII never writes, renames, locks, moves or deletes
it, and never creates a file beside it.
enforced_by: none: judgement
source: director

### Finding the file

| Platform | Automatic candidates, in order | File picker |
|----------|--------------------------------|-------------|
| Windows | 1. `mucka.ini` in Mucka's install directory; 2. `%USERPROFILE%\mucka.ini` | offered |
| Linux, macOS | `$HOME/mucka.ini` | offered |
| Android | none. Mucka's file is private to package `org.kfs.mucka` | the only route |

source: director, reference

FACT The Windows order matches Mucka's own lookup: beside its executable first, then the user
profile. The first candidate that exists is the file Mucka itself was using.
source: reference

FACT Mucka never shipped on Linux or macOS, so the `$HOME/mucka.ini` candidate there only finds a
file the user copied in by hand.
source: reference

FACT Mucka's Windows installer (Inno Setup, 64-bit) registers the application id
`{6E8A4F2C-3B1D-4E9A-8C7F-2A5D0E3B6F1C}`; its uninstall registry key is that id with `_is1`
appended. Its default install directory is `Mucka` under Program Files, `C:\Program Files\Mucka`
for an all-users install.
source: reference

| Step | Where to look for Mucka's install directory |
|------|---------------------------------------------|
| 1 | `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{6E8A4F2C-3B1D-4E9A-8C7F-2A5D0E3B6F1C}_is1`, value `InstallLocation` |
| 2 | the same key under `HKCU` (a per-user install) |
| 3 | `%ProgramFiles%\Mucka` (the expanded default) |

source: reference

EVIDENCE On the director's machine, with Mucka 0.22.1 installed: the HKLM key above exists, with
`DisplayName` `Mucka version 0.22.1`, `InstallLocation` `C:\Program Files\Mucka\` and
`Inno Setup: App Path` `C:\Program Files\Mucka`. There is no `mucka.ini` in that directory. The file
in use is `%USERPROFILE%\mucka.ini`.
source: director (the director's machine, observed read-only)

open: when both Windows candidates exist, should the importer take the first silently, or show both
and let the user choose?

### What is imported

| Item | Rule |
|------|------|
| Profiles | in MRU order, as `mucka-ini.md` "`[profiles]`" defines it, with every identity key |
| Settings | per profile, keeping the global / "profile only" split for each part, as `mucka-ini.md` "How the sections combine" defines it |
| F-keys | `F1`-`F36`, global and per profile, same split |
| `[watch]` | the whole `[watch]` family as raw lines, verbatim, comments included |
| Keys for `later` features | imported and kept, unused until the feature is built |
| Obsolete keys | dropped (`mucka-ini.md`, "Obsolete keys") |
| Unknown keys and sections | ignored |
| Orphan `[settings:Name]` / `[fkeys:Name]` | ignored |
| Passwords | not importable. They are not in the ini. The user enters each password once, the first time that profile is used. A `rememberpassword=yes` profile then stores it in MuckaII's own secure storage |
| `mucka.db` | not imported |

source: director (profiles in MRU order, settings, F-keys, `[watch]` verbatim, passwords,
`mucka.db`: `INTERNAL.md`), reference (how each is read). The four rows from "Keys for `later`
features" to "Orphan" are proposals; see the `open:` lines in `mucka-ini.md`.

RULE The importer never fails as a whole because of one bad value. A malformed value takes the
default, as Mucka's own reader did.
enforced_by: none: judgement
source: reference

open: does declining the import (or cancelling the picker) write the marker, or is it offered again
on the next run? And can the user re-run the import later from a menu?

open: does the importer show the user a summary of what it imported (profile count, F-keys, watch
rules)?
