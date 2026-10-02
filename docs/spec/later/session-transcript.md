# Later: session transcript and recording chip

FACT `scope: later`. The wire log is always on and is launch (`persistence.md`, Wire log). The
transcript is a separate file, armed by hand.
source: director

## Transcript

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

FACT File name examples: host `mud2.co.uk` at 2026-09-14 20:30:15 gives
`session-rec.mud2.co.uk.20260914-203015.txt`. Host `a/b` gives `session-rec.a_b.20260914-203015.txt`.
A blank host gives `session-rec.unknown.20260914-203015.txt`.
source: reference

open: header and footer wording for MuckaII (the reference names "Mucka"), and whether the file
name keeps the `session-rec.` prefix.
default: `Recording by MuckaII from ...`, and the file name keeps the `session-rec.` prefix.

## Recording chip

| State | Wide | Compact | source |
|-------|------|---------|--------|
| Idle | hollow chip, #2D333B border, `rec` in #666666 | `(R)` in #666666 | reference |
| Recording | #C02020 border, U+25CF in #E74856, `rec` in white | `(R)` in #E74856 | reference |
| Tap | start or stop the plain-text transcript of the screen | same | reference |

FACT The same toggle is in the menu as "Log Recording". A `-record` command-line flag arms it on
first entering the game (`connection.md` 6). The transcript is separate from the wire log, which is
always on.
source: reference

open: `INTERNAL.md` lists the wire log at launch, not a transcript. Is the transcript (and so this
chip) launch or later?
default: later, as this file's place says.

FACT Related entries elsewhere: the chip's place in the status bar is `ui.md` 3.1 and 3.6; the
`logttr` debug key writes reset-timer readings into the transcript (`mucka-ini.md`).
source: director
