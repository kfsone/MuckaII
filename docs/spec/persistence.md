# Persistence

| Feature | Scope |
|---------|-------|
| Data directory | `scope: launch` |
| Wire log | `scope: launch` |
| Schema forward-compatibility | `scope: launch` |
| First-run import of `mucka.ini` | `scope: launch` |
| Session transcript (`rec`) | `scope: later`: `later/session-transcript.md` (open: OPEN E1) |

source: director

## Data directory

DECISION Runtime data lives in `~/.muckaii/`.
source: director

| Platform | Directory |
|----------|-----------|
| Windows | `%USERPROFILE%\.muckaii\` |
| Linux, macOS | `$HOME/.muckaii/` |
| Android | the app's private files directory. Android has no home directory |

source: director, reference (the Android row is inferred from Mucka's Android behaviour)

RULE On Android, data goes in the app's private files directory, never its cache directory: Android
empties a cache directory under storage pressure or on "Clear cache", and the wire log must survive.
enforced_by: none: judgement
source: reference

| Contents | What it holds |
|----------|---------------|
| one SQLite database | the wire log, and any later store that joins it |
| the settings file | MuckaII's own settings, profiles, F-keys and the imported `[watch]` text, in MuckaII's own format |
| the import marker | written once when the `mucka.ini` import is finished (see "Import") |
| session transcripts | `scope: later`. One text file per recording (`later/session-transcript.md`) |

open: proposed contents. Only the SQLite wire log and the import are director rulings
(`INTERNAL.md`); the settings file and the marker follow from them.
default: the contents in the table above.

open: file names inside `~/.muckaii/` (database, settings file, marker, transcript location).
default: the database is `muckaii.db` and the import marker is `mucka-ini-imported`, both directly in
`~/.muckaii/`. The settings file takes the base name `settings`, with the extension of whatever format
`persistence` gives it. Transcripts (later) go in `~/.muckaii/` itself, as the transcript's Location
row gives.

FACT Mucka kept its data in `~/.mucka/` on Windows (one database, `mucka.db`) and in the app's
private files directory on Android. MuckaII does not read, import or modify `mucka.db`.
source: reference, director

## Wire log

RULE The wire log is always on. There is no setting to turn it off, nothing to arm, and no UI to
enable it.
enforced_by: none: judgement
source: director

RULE Every byte in both directions is recorded as it came off or went onto the socket, with the two
masked sends below as the only exception. That includes telnet negotiation, C1 sequences, ANSI and
the login exchange. Nothing is decoded, filtered, compressed or reframed before it is stored.
enforced_by: none: judgement
source: director

DECISION The outbound password line and the outbound account ID line are masked in the wire log.
Each is stored as a sent record of the same length, with every byte before the line's `0D 0A`
replaced by a placeholder byte, and is followed by an annotation naming what was masked
(`masked: password`, `masked: account id`). Rejected: keeping them verbatim (Mucka).
source: director

| Masked send | How the client knows it is that send |
|-------------|--------------------------------------|
| password | the login state machine sends it in reply to the recognised password prompt (`connection.md` 3.1, step 3) |
| account ID | the login state machine sends it in reply to the recognised account prompt (`connection.md` 3.1, step 2) |

source: director

DECISION Only the password needs masking. A send the user originates (typed, macro, hotkey, raw
console; `architecture.md`, the origin table) is masked in the wire log, same length, every byte
before the `0D 0A` replaced, in exactly two cases: (a) it is sent before login completes, that is
before the first Option menu (`connection.md` 3.1, step 4); (b) it answers a recognised password
prompt, or a recognised account prompt (root `CLAUDE.md`: account IDs are always masked), at any
time. Everything else is logged verbatim: the pre-game shell after login (mail, the persona menu,
the examine sub-shell) and the game. The annotation names the recognised prompt (`masked: account
id`, `masked: password`), else `masked: line before login`. Case (a) covers type-ahead (a line
typed while connecting waits in the UI -> core FIFO and is sent before any prompt exists) and a
credential typed at a login prompt the drive does not recognise ("Enter password:", "PASSWORD:");
it also masks the shell's own login exchange and anything else typed before the first Option menu.
The trade-off, accepted: after login, a password typed at a prompt that is not recognised (the
shell's "Old Password:" and "New Password:" when changing the password) is in clear in the wire log,
in the command box and in the recall list alike. The rule relies on one connection per `Core`
(`core::Core`, single use): login completes once and never re-arms, so a second login over the same
session (were one ever built) would log its shell login and unrecognised prompts in clear. A line
core judged masked when it was submitted stays masked however the state moves before it is sent
(`architecture.md`, Input: core's verdict). A frontend shows `*` in the command box while login is
incomplete (every line is masked then, type-ahead included) and while a recognised password prompt
is live, and keeps masked lines, only those, out of the recall list. Invariant #1's acceptance
check skips masked records. Rejected:
masking every pre-game line (the pre-game shell could not be used or read back); masking only while
a recognised prompt is live (leaks type-ahead).
source: director ("only the password needs masking"); agent, on a review finding (the type-ahead
window before login)

open: The account ID also arrives inbound: in the server's echo of the account line and in the
`95` client-mode block (`events.md`, `client_mode`). Mask it in received records too, or keep them
verbatim?
default: keep received records verbatim until the director rules.

RULE The log opens with the connection, before the first byte, so the login exchange is in it.
enforced_by: none: judgement
source: reference

RULE Recording never stalls typing or the screen (root `CLAUDE.md`, Invariant #1). The socket and
UI threads only hand a record over; one background writer does all the disk work.
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
default: the instance that first creates a lock file beside the marker, using an exclusive create,
imports. The other does not offer the import in that run.

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
| `1` sent | one socket write | raw bytes. 7-bit once past the send boundary |
| `2` annotation | one client-side note | UTF-8 text |

source: reference

RULE Sequence order, timestamp order and storage order agree for every record in a run. They are
taken at one serialisation point, so a record never carries an earlier sequence number with a later
timestamp.
enforced_by: none: judgement
source: reference

DECISION A record's timestamp is the run's start, read once from the wall clock (unix ms UTC), plus
the time elapsed since on the monotonic (steady) clock. A wall-clock step during a run (NTP, a manual
change) therefore never makes timestamps run backwards against the sequence; the cost is drift from
wall time over a long run. The run's `ended` uses the same clock. Rejected: reading the wall clock
for every record, which can run backwards.
source: agent, on a review finding

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
| client version | the MuckaII version that wrote it |

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

open: which annotations MuckaII writes.
default: every row above except the reset-timer diagnostics (`logttr` is a later debug key), plus
the masking annotations ("Wire log") and the probe and raw-console marks (`architecture.md`,
`origin` table).

### What the user can do with it

FACT The database is a plain SQLite file. The user can open it with any SQLite tool while the client
runs, and one query returns the traffic of a run in order, with timestamp, direction and bytes. The
data column of a record prints as the line itself.
source: reference

FACT MuckaII's table and column names are its own. The fields above are the requirement, not a
schema.
source: agent

| Failure | Behaviour |
|---------|-----------|
| The database cannot be opened | the connection still goes ahead. The failure is reported once, in the terminal, and no records are kept for that run |
| A write fails during a run | the log stops for the rest of the run, says so once in the terminal, and stops accepting records. No retry loop and no error line per record |
| The client is closed | queued records are flushed before exit, with a bounded wait |

source: reference

DECISION Three refinements of the table above. (1) A lock held by another instance (`SQLITE_BUSY`,
`SQLITE_LOCKED`) is not a failure: the writer keeps the batch and retries, backing off from 50 ms
to 2 s, at open and at every write, and reports nothing; only other errors stop the log. (2) The
hand-over queue holds at most 16384 records (`persistence::kMaxQueuedRecords`) and at most 64 MiB of
their bytes (`persistence::kMaxQueuedBytes`). A record handed over that would pass either bound is
dropped at once, never waited for: its sequence number stays used, so the gap shows, the first drop
is reported once in the terminal, and the next record that fits is preceded by an annotation
`wire log: N records dropped here; the writer fell behind`, where N is the number of dropped records
(the width of the gap). The count annotation uses a sequence number only when it is stored. (3) The bounded wait at
close is 3 s; after it the writer finishes only the batch in hand (at most 512 records, one 250 ms
lock wait) and the rest is dropped. Rejected: a permanent stop on a transient lock (two instances
share one database by design); an unbounded queue; blocking the core thread on a full queue.
source: agent, on review findings (a lock disabled the log; the queue and the close wait were
unbounded)

DECISION No home directory (`USERPROFILE` on Windows, `HOME` elsewhere, unset or empty) means no
data directory: the connection goes ahead, no records are kept, and the run says so once in the
terminal, as for a database that cannot be opened. The database path reaches SQLite as UTF-8, never
through the ANSI code page.
source: agent, on a review finding

FACT Nothing prunes, rotates or consolidates the wire log. The user reclaims space by deleting rows
by hand.
source: reference

EVIDENCE Measured on Mucka over 13 sessions, 18.11 play-hours: 119,921 records, 10,141,176 bytes of
payload, 0.156 KB/s average. Received 76,633 records (9,668,270 bytes), sent 42,946 records
(465,243 bytes, about 11 bytes per typed command), 342 annotations. Stored one row per record with no
indexes, about 0.74 MB per play-hour.
source: reference

FACT In Mucka's wire log the password record is stored in clear in the sent direction. The server
does not echo it, so it is absent from the received direction and from the screen.
source: reference

FACT A reference wire-log record pair showing the echo: sent `wave\r\r\n`, then received
`wave\r\r\n`.
source: reference

This pair is the source of the outbound-terminator question owned by `connection.md` 1 (OPEN A1).

## Schema forward-compatibility

FACT MuckaII is released on GitHub. Its databases live on users' machines that nobody here can
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

FACT The reference's schema history includes a step that deleted rows; the rule above is stricter.
source: reference

RULE The schema is never converged on whatever shape the file happens to have: a rule re-evaluated
at every open keeps acting on a user's file, unprompted and with no backup.
enforced_by: none: judgement
source: reference

open: what a client does with a database written by a NEWER MuckaII (unknown later steps recorded).
Read-only, refuse, or carry on.
default: carry on logging, and say so once in the terminal. Steps are additive, so the tables this
version knows are still there. It never runs, repeats or undoes a step it does not know. The wire log
stays always on.

## Retention and deduplication

FACT The wire log fills with repeats as the player walks around, such as the same long room
descriptions over and over.
source: director

DECISION Wire payloads are stored content-addressed from the first release. One table maps a hash
to the bytes, and each wire row references a payload by its hash, so repeated text is stored once.
It is in the first schema because no later migration may rewrite a user's database destructively.
Retention policies and compaction come later, on top of this table. Rejected: storing each payload
inline per row (Mucka's layout).
source: director

open: The retention policy. What ages out, after how long, and whether old payloads are
compressed.
default: nothing ages out and nothing is compressed, as "What the user can do with it" states.
Retention is `scope: later` (`INTERNAL.md`, Scope).

## Settings storage

DECISION A settings store holds only the keys the user has changed. A missing key means "use the
built-in value". This lets the UI show that a setting is at its built-in value, and lets a later
release say "this setting's built-in value changed: keep or switch?" (`flow.md`, Settings).
source: director

DECISION Settings resolve through three layers, one key at a time, strongest first:

| Layer | Holds | Edited by |
|-------|-------|-----------|
| profile | the keys this profile sets | the profile page (without logging in), Save, Save to this profile |
| global | the keys set for every profile | global settings, Save as default (`flow.md`, Settings) |
| built-in | every key's built-in value | nobody; it ships with MuckaII |

A key a profile does not set falls through to the global layer, and from there to the built-in
value. Rejected: a per-profile section that replaces the global settings wholesale (Mucka's
semantics, see `mucka-ini.md` "How the sections combine").
source: director

DECISION A layer can hold a text-valued key (an F-key slot, or any other key whose value is text)
as `cleared`: an explicit empty value that stops the fall-through. Numeric and boolean keys have
no `cleared` value. A profile that holds F-key slot 7 as `cleared` has slot 7 empty even when the
global layer fills it. Removing a key from a layer (Reset to inherited) is different: the key falls through again. A
`cleared` F-key slot counts as empty for `defaulthotkeys`, so a cleared F1-F5 slot takes its
built-in macro while `defaulthotkeys` is on, exactly as an empty slot does in Mucka (`mucka-ini.md`,
`[profile:Name]`). Rejected: treating an empty string as "not set", which leaves no way for a
profile to empty a slot the global layer fills.
source: agent (follows from the director's per-key layering ruling)

RULE The importer keeps each Mucka profile's *effective* values. Mucka replaces wholesale while
MuckaII layers, so:

| Mucka part present for profile P | What the importer writes to P's profile layer |
|----------------------------------|-----------------------------------------------|
| `[settings:P]` | a profile-level value for every settings-scope key whose Mucka value differs from what MuckaII's layering would otherwise produce |
| `[fkeys:P]` | all 36 slots explicitly: the slot's text from `[fkeys:P]`, or `cleared` where `[fkeys:P]` lacks the slot. An empty `[fkeys:P]` gives 36 `cleared` slots |
| neither | nothing: P inherits the global layer, as it did in Mucka |

The F-key test: after import, editing a global F-key slot changes no profile that had its own
`[fkeys:P]`.
source: agent (follows from the director's per-key layering ruling)
enforced_by: none: judgement

open: For a profile with `[settings:P]`, a settings-scope key that matched the global value at
import is not written, so a later global edit reaches that profile, which Mucka's replace semantics
would not have allowed. Write every settings-scope key explicitly for such a profile instead?
default: keep the rule above (differences only).

## Session transcript (`rec`)

FACT `scope: later`: `later/session-transcript.md`.
source: director

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

FACT The Windows order is Mucka's own lookup, so the first candidate that exists is the file Mucka
was using. On Linux and macOS `$HOME/mucka.ini` only finds a file the user copied in by hand
(`mucka-ini.md`, Where Mucka kept it).
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
default: take the first silently. It is the file Mucka itself was using.

### What is imported

| Item | Rule |
|------|------|
| Profiles | in MRU order, as `mucka-ini.md` "`[profiles]`" defines it, with every identity key |
| Settings | per profile, keeping the global / "profile only" split for each part, as `mucka-ini.md` "How the sections combine" defines it, written as explicit per-key values ("Settings storage") |
| F-keys | `F1`-`F36`, global and per profile, same split. A profile with `[fkeys:Name]` gets all 36 slots as explicit values, `cleared` included ("Settings storage") |
| `[watch]` | the whole `[watch]` family as raw lines, verbatim, comments included |
| Keys for `later` features | imported and kept, unused until the feature is built |
| Obsolete keys | dropped (`mucka-ini.md`, "Obsolete keys") |
| Unknown keys and sections | ignored |
| Orphan `[settings:Name]` / `[fkeys:Name]` | ignored |
| Passwords | not importable. They are not in the ini. The user enters each password once, the first time that profile is used. A `rememberpassword=yes` profile then stores it in MuckaII's own secure storage |
| `mucka.db` | not imported |

source: director (profiles in MRU order, settings, F-keys, `[watch]` verbatim, passwords,
`mucka.db`: `INTERNAL.md`), reference (how each is read), agent (the per-part "profile only" split
and the explicit per-key and `cleared` values in the Settings and F-keys rows). The four rows from
"Keys for `later` features" to "Orphan" are proposals; see the `open:` lines in `mucka-ini.md`.

RULE The importer never fails as a whole because of one bad value. A malformed value takes the
default, as Mucka's own reader did.
enforced_by: none: judgement
source: reference

open: does declining the import (or cancelling the picker) write the marker, or is it offered again
on the next run? And can the user re-run the import later from a menu?
default: declining writes the marker. Cancelling the picker does not, so the import is offered
again on the next run. There is no menu item to re-run it.

open: does the importer show the user a summary of what it imported (profile count, F-keys, watch
rules)?
default: yes, as one client line: the number of profiles, F-key slots and `[watch]` lines imported.
