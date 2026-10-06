# Connection: transport, telnet, login, width, probes

FACT Byte values are hex. `ESC` is `1B`. `CRLF` is `0D 0A`. `$-` (the director's and Bartle's
notation) is the two bytes `ESC -` (`1B 2D`), written with no spaces: `$-[FES$-]` is exactly
`1B 2D 5B 46 45 53 1B 2D 5D`, with no space after `$-[` or before `$-]`.
source: director; bartle:FE4 2)

FACT No `.c1` capture has a `tx` record, an IAC byte, or a login prompt; `phone-login-at-20-columns.c1`
starts at the first game-mode FES reply. Every telnet and login row below rests on Bartle, Clio or
the reference.
source: capture:phone-login-at-20-columns.c1

## 1. Transport

RULE MuckaII connects over plain TCP. No TLS: the far end has none.
enforced_by: none: judgement
source: director

| Item | Value | Scope | Source |
|------|-------|-------|--------|
| Default profile "MUD2 UK" | host `mud2.co.uk`, port 23 | now | reference |
| Default profile "MUD2.COM" | host `www.mud2.com`, port 27723 | now | reference |
| New-profile default | host `mud2.co.uk`, port 23 | now | reference |
| Port when none given | 23 | now | clio:src/main.c (CLIOPORT fallback); reference |
| Connect timeout | 15 s overall, name resolution included, then the connect page shows the error text | now | reference; agent (overall) |
| Nagle | off: each write goes out immediately | now | reference |
| Outbound line terminator | `0D 0A` | now | clio:src/telnet.l autologin rules; reference |
| Outbound order | one ordered writer; every typed line, macro, probe and telnet reply goes through it | now | reference; root CLAUDE.md Invariant #1 |

open: Outbound terminator. A reference wire-log pair (`persistence.md`) shows `wave\r\r\n` (CR CR
LF) sent and echoed, against `0D 0A` above and the `\r\n` echo in the captures (`protocol.md`, Echo).
Which does the server receive?
default: `0D 0A`, as the table above, Clio and the captures' echo give it.

DECISION The connect never blocks the core thread. On Windows name resolution is
`GetAddrInfoExW`, asynchronous: its completion routine wakes the core, and a quit or the deadline
cancels it with `GetAddrInfoExCancel` and waits for the routine, so nothing outlives the connect.
On POSIX `getaddrinfo` cannot be cancelled (`getaddrinfo_a` is glibc only, absent from macOS's
libc and Android's bionic), so it runs on a short-lived detached thread that holds only its own
result and the wake socket, and an abandoned lookup finishes harmlessly after the connect has returned. The connect
itself is non-blocking and polled together with the wake socket. A quit while connecting therefore
returns at once. Rejected: a blocking connect with a per-address timeout (a quit waited for it, up
to 15 s per address); a detached thread on Windows, where cancellation exists.
source: agent

RULE The 15 s connect timeout bounds resolution and every address tried, together. Each address
gets an equal share of what is left when its attempt starts, split over the addresses not yet
tried, itself included (`transport::getAttemptBudget`); the last gets all that is left. An address
that never answers therefore cannot spend the budget of the addresses after it.
enforced_by: none: judgement
source: agent

RULE A data byte 255 in an outbound line is sent doubled (`FF FF`), as telnet requires
(`protocol.md`, Telnet residue in the stream). The 7-bit filter means a typed line never holds one;
NAWS doubles its own (2.2).
enforced_by: none: judgement
source: protocol.md (bartle:FE4; clio:src/telnet.l)

RULE A quit (Ctrl+C, closing the window) writes the lines already submitted before the socket
closes, within a bounded wait of 2 s. Any typed line not written by then, or left when the
connection closes or never opens, is reported once in the terminal (`N typed lines were not sent:
<why>`) and in the wire log; no typed line is lost silently.
enforced_by: none: judgement
source: agent

### 1.1 Disconnect detection

| Event | What the user sees | Scope | Source |
|-------|--------------------|-------|--------|
| Server closes the socket (read of 0 bytes) or the socket errors | An alert titled "Disconnected" with the text "The server closed the connection.", then the profile/connect page. In command-line direct-connect mode (section 6), the app closes instead. | now | reference |
| Local disconnect (user quits, page closes, a new connect replaces the old one) | No alert. The session state is cleared the same way. | now | reference |
| Any disconnect | The probe timer stops, the anti-idle timer stops, telnet one-shot guards and login state re-arm for the next connection. The advertised window size is kept. | now | reference |
| Reconnect | There is no automatic transport reconnect. The user connects again from the profile page. | now | reference |

### 1.2 Session-drop classification (game mode exited, connection still up)

RULE MuckaII expects no dedicated code for a drop out of the game back to the Option menu. It reads
the landmarks below from the in-game text. It collapses whitespace (including the NUL padding the
server adds to wrapped rows) and joins up to 3 wrapped rows, so the landmark matches at any width
down to 20 columns. The first landmark seen wins.
enforced_by: none: judgement
source: reference

| Order | Reason | Signal | Match rule | Source |
|-------|--------|--------|------------|--------|
| 1 | Reset | C06 C06 "Something magical is happening." | code; or the whole line equal to that text, case-insensitive | bartle:FE4 06 06; reference |
| 2 | Permadeath | C08 C13 ("Not updating persona.") | code | reference |
| 3 | Quit | line "Cheerio!" | whole line equal, case-insensitive. `Ann says "Cheerio!"` must not match. | reference |
| 4 | Died | end-of-game summary with no farewell | line starts `Overall, you ` and ends ` points this game.`. The verb (scored/lost) is not matched. | reference |
| 5 | Unknown | none of the above | idle boot, kick, anything else | reference |

FACT Drop headline overlay and automatic relogin after a Reset drop: `scope: later`,
`later/drop-overlay.md`.
source: director

RULE Drop classification is `scope: now`, because a Reset clears the dreamword (section 5.4).
enforced_by: none: judgement
source: reference

open: Are the drop headline overlay and the automatic relogin after a Reset drop `scope: now`, as
managed persona selection (3.4) is? The Tombstone (`flow.md`) is the now-scope screen for a death.
default: both `scope: later`.

## 2. Telnet options

FACT Option codes: ECHO `01`, SGA `03`, TTYPE `18`, NAWS `1F`, NEW-ENVIRON `27`. Commands: IAC `FF`,
DONT `FE`, DO `FD`, WONT `FC`, WILL `FB`, SB `FA`, SE `F0`.
source: clio:src/telnet.l

### 2.1 Replies

| Received | MuckaII sends | Basis |
|----------|---------------|-------|
| `FF FD 01` DO ECHO | `FF FC 01` | Clio and reference agree |
| `FF FD 03` DO SGA | `FF FC 03` (WONT) | default below; the reference sends `FF FB 03` (WILL), every time |
| `FF FD 18` DO TTYPE | `FF FB 18`, first time only; repeats ignored | Clio and reference agree |
| `FF FD 1F` DO NAWS | `FF FB 1F` then the NAWS SB (2.3), first time only | Clio and reference agree; the SB size is 2.3's default |
| `FF FD 27` DO NEW-ENVIRON | `FF FC 27` (WONT) | default below; the reference sends `FF FB 27` when the login name is non-empty, first time only |
| `FF FD xx` any other | `FF FC xx` | Clio and reference agree |
| `FF FE xx` DONT any | nothing | Clio and reference agree |
| `FF FB 01` WILL ECHO | nothing | Clio and reference agree |
| `FF FB 03` WILL SGA | `FF FD 03`, first time only | default below; the reference answers every time |
| `FF FB xx` WILL any other | `FF FE xx` | Clio and reference agree |
| `FF FC xx` WONT any | nothing | Clio and reference agree |
| `FF F9` GA | nothing; ignored | Clio and reference agree |
| `FF FA 18 01 FF F0` TTYPE SEND | TTYPE IS "ansi" (2.2) | Clio and reference agree (WILL TTYPE is always sent first) |
| `FF FA 27 01 ... FF F0` NEW-ENVIRON SEND | nothing (WONT was sent) | default below; the reference sends NEW-ENVIRON IS USER (2.2) |
| any other SB | nothing; ignored. An SB that reaches 1024 bytes is abandoned (`protocol.md`, Telnet subnegotiation) | Clio and reference agree; the ceiling is the reference's |
source: clio:src/telnet.l {IAC}{DO}. {IAC}{WILL}. {IAC}{DONT}. {IAC}{WONT}. {IAC}{SB}{TERMINAL_TYPE}{SEND}{IAC}{SE}; reference

EVIDENCE Clio's DO handler has no case for option 3. DO SGA falls to `default: twont`, so Clio
replies `FF FC 03`.
source: clio:src/telnet.l {IAC}{DO}.
open: DO SGA. Clio refuses with WONT, the reference agrees with WILL. Which one does MuckaII send?
default: `FF FC 03` (WONT), as Clio does.

EVIDENCE Clio refuses NEW-ENVIRON (it lists 39 with the other refused options).
source: clio:src/telnet.l {IAC}{DO}.
open: NEW-ENVIRON. The reference's login name defaults to `mud`, so in practice it always sends
WILL and then advertises USER. If the server accepts USER from NEW-ENVIRON, the shell `login:`
prompt may never be shown. No capture shows which happens on either server.
default: `FF FC 27` (WONT), as Clio does. The shell `login:` prompt is then answered by auto-login
step 1 (3.1).

EVIDENCE Clio guards DO SGA with a one-shot flag. The reference answers every WILL SGA.
source: clio:src/telnet.l {IAC}{WILL}.
open: WILL SGA. Is it answered once (Clio) or every time (reference)?
default: once per connection, as Clio does.

FACT A literal `FF FF` in the stream is one data byte 255 after telnet decoding, and is owned by
`protocol.md`: in client mode it is part of the C1 terminator.
source: bartle:FE4 Codes and their meanings

### 2.2 Subnegotiation contents

| SB sent | Bytes |
|---------|-------|
| TTYPE IS "ansi" | `FF FA 18 00 61 6E 73 69 FF F0` |
| NAWS (width W, height H) | `FF FA 1F Wh Wl Hh Hl FF F0`. Any byte equal to `FF` among Wh Wl Hh Hl is sent doubled (`FF FF`). |
| NEW-ENVIRON IS USER=<name> | `FF FA 27 00 00 55 53 45 52 01 <name, 7-bit bytes> FF F0` (IS=00, VAR=00, "USER", VALUE=01) |
source: clio:src/tterm.c; clio:src/tnaws.c; reference

FACT Login names are alphanumeric, so the NEW-ENVIRON value never holds a byte 00-03 and nothing in
it is escaped.
source: reference

### 2.3 NAWS size and resend

| Item | Clio | Reference |
|------|------|-----------|
| First NAWS | `FF FA 1F 00 50 00 15 FF F0` (80 x 21, fixed) | effective columns (section 4) x 21 |
| Height | 21 | 21, always; rows are never measured |
| Resent when | never | (a) the effective column count changes, if NAWS was agreed; (b) immediately before client-mode entry, even if unchanged |
| Across reconnect | n/a | the one-shot guard re-arms; the last size is kept |
source: clio:src/telnet.l {IAC}{DO}.; clio:src/tnaws.c; reference

EVIDENCE Clio always advertises 80 x 21 and never resends.
source: clio:src/telnet.l {IAC}{DO}.
open: NAWS. Should it carry the real column count and resend on resize (reference), or stay a
fixed 80 x 21 (Clio)? Bartle is silent on NAWS, and no capture holds an IAC byte, so nothing on the
wire separates the effect of NAWS from that of `/T`. What NAWS changes on the server is unconfirmed
(section 4.1).
default: the reference's form: the effective column count x 21, sent from the start of the
connection and resent when it changes, with the same column count as the `/T` width 3.2 chose.
Clio's fixed 80 x 21 goes with its ESC-T entry, which 3.2 rejects.

## 3. Login

### 3.1 Prompts and replies (auto-login)

RULE Auto-login runs only when the profile has the telnet-login switch on (ini key `telnetlogin`,
default yes), an account ID, and a password. With the switch off, the account ID is put into the
command box for the user to send by hand, and nothing is sent automatically. With an account ID but
no stored password, a password dialog appears before connecting. It has a "remember" option, which
stores the password in the profile settings in plaintext (`persistence.md`; `INTERNAL.md`,
Credentials and threat model).
enforced_by: none: judgement
source: reference; director (plaintext storage)

| Step | Prompt recognised | Clio match | Reference match | Sent | Once? |
|------|-------------------|------------|-----------------|------|-------|
| 1 | Unix shell login | literal `login: ` anywhere | `login:` contained, case-insensitive | login name + `0D 0A` (default name `mud`) | Clio: once. Reference: every time it appears. |
| 2 | MUD2 account | `Account ID: ` at line start | `account id` contained, case-insensitive | account ID + `0D 0A` | once |
| 3 | Password | `Password:` or `password:` at line start | `password` contained, case-insensitive | password + `0D 0A` | once |
| 4 | Option menu | `Option: ` or `Option (H for help): ` at line start | `Option` contained, case-sensitive | client-mode entry (3.2) | once per login |
source: clio:src/telnet.l "login: " ^"Account ID: " ^[Pp]"assword:" ^"Option: "; reference

| Matching detail | Clio | Reference |
|-----------------|------|-----------|
| Where it matches | lexer over the byte stream, so a prompt split across TCP reads still matches | outside game mode, after each TCP read, against the unterminated partial line accumulated by that read. Each read emits and clears the partial. |
| Password ordering | the shell password is sent only after the shell login name went, and the MUD2 password only after the account ID went | no ordering guard |
| Shell password | separate value (`-c` / `CLIOLPASS`), sent at `Password:` after the shell login | none |
| Disarm | per-prompt sent-flags | every prompt disarms permanently on the first game-mode entry; all re-arm on disconnect |
source: clio:src/telnet.l; clio:src/main.c; reference

EVIDENCE Clio matches exact anchored literals and keeps each prompt one-shot. The reference matches
case-insensitive substrings, does not make `login:` one-shot, and has no ordering guard.
source: clio:src/telnet.l
open: Prompt matching. A substring match on `password` also hits server text such as "Old
Password:" or a MOTD line containing the word. A prompt split across two TCP reads is missed by the
reference. Follow Clio (anchored literals, one-shot, ordered) or the reference?
default: Clio's matching: anchored literals over the byte stream, each prompt one-shot, the password
sent only after the account ID, never at an "Old Password:" line.

EVIDENCE A live login to the UK server (port 23) by MuckaII on 2026-10-05. These are the received
texts, in order. Each prompt arrived as an unterminated partial line, and whether any prompt was
split across reads was not recorded. The machine name in the shell prompt is shown as `<host>`:

| Step | Received |
|------|----------|
| Shell login | `<host> login: ` (MuckaII answered `mud`) |
| Account | `Please enter your account id (starting Z000) now.\r\nType QUIT to abort login.\r\nAccount ID: ` |
| Password | `Password: `, straight after the server echoed the account line |
| Option menu | `...[Checking mail...]\r\n[You have mail: 1 item]\r\nMUD login menu.\r\nOption (H for help): ` |
| After client-mode entry | `ESC[1;37;40m`, code `00` (`9B FF FF`), the `95` block, `[New terminal width is 80]\r\n`, then `Option (H for help): ` again |

source: director's live session, recorded in the MuckaII wire log on 2026-10-05

EVIDENCE From the same session, the server's telnet offers were: DO TTYPE, DO TSPEED (0x20), DO
XDISPLOC (0x23), DO NEW-ENVIRON, DO LINEMODE (0x22), WILL SGA, DO ECHO, DO NAWS, WILL STATUS
(0x05), DO LFLOW (0x21). The shell `login:` prompt still appeared.
source: director's live session, recorded in the MuckaII wire log on 2026-10-05

RULE A "(y/n)" question before the banner, e.g. "Skip the rest? (y/n)" on mud2.com, or a
usurp-session question, is not answered by auto-login; managed persona selection answers `y` (3.4).
enforced_by: none: judgement
source: reference

### 3.2 Client-mode entry

DECISION Client-mode entry is the reference's form, no CRLF:
`1B 2D 5B 1B 06 1B 2D 4E 2F 54 38 30 1B 2D 5D` = `$-[` ESC ^F `$-N/T{cols}$-]` (cols = 80 shown: `/T80`).
Bartle lists ESC ^F and ESC-N ("user defines wrap") and is silent on `/T`, so this deprecates no
Bartle statement. The capture decides between the two clients: the server wraps at 20 columns
there, which ESC-T (fixed at 78 wide) cannot produce. Rejected: Clio's bare `1B 06 1B 2D 54`
(ESC ^F ESC-T).
source: director (evidence ranking, CLAUDE.md); capture:phone-login-at-20-columns.c1; reference

EVIDENCE Clio sends ESC ^F ESC-T (text mode, 78 wide), not ESC-N.
source: clio:src/telnet.l ^"Option: "

| Escape (client to server) | Meaning |
|---------------------------|---------|
| `1B 2D 5B` ESC-[ | command interrupt start; contents are executed, not echoed |
| `1B 2D 5D` ESC-] | command interrupt end |
| `1B 06` ESC ^F | enter client/server mode at maximum compatibility |
| `1B 2D 54` ESC-T | text mode: colour ANSI, 21 lines, 78 wide |
| `1B 2D 47` ESC-G | graphics mode: colour ANSI, 18 lines, 57 wide |
| `1B 2D 4E` ESC-N | normal mode: user defines wrap |
| `1B 2D` i `49` ESC-iI | identify self with key i (unused) |
source: bartle:FE4 2)

FACT Bartle's two entry forms: `/M1` + `0D 0A` at the "Option:" prompt, every login; or ESC ^F
(`1B 06`), accepted transparently at any prompt, not taken as input, at maximum compatibility.
`/T` is not in Bartle's documents; its only sources are the reference and the capture.
source: bartle:FE4 1)

EVIDENCE After entry at 20 columns, the server wraps every game line at 20 columns or fewer. Wrapped
rows end `0D 00 0D 0A`. Examples: "You're already" / "getting object" / "identification".
source: capture:phone-login-at-20-columns.c1

RULE MuckaII resends NAWS (2.3) and then sends client-mode entry, once per login, at the first
Option prompt seen while not in game mode.
enforced_by: none: judgement
source: reference

| Server reply to entry | Handling | Source |
|-----------------------|----------|--------|
| Licence, min/max compatibility, account ID, privilege level, bracketed by C95 | owned by `protocol.md` | bartle:FE4 1) |
| `ESC-` i `W` (new screen width) | read as confirmation of the width; a mismatch is logged | bartle:FE4 2); reference |
| line `[New terminal width is N]` | swallowed, not shown; read as confirmation | reference |
| a second Option prompt | the shell is now idle and ready for commands | reference |

### 3.3 Game mode entry and exit

RULE The edges of game mode are `protocol.md`, Game mode. On entry MuckaII sends the first probe
beat (5.2) and the setup batch (5.3).
enforced_by: none: judgement
source: reference

### 3.4 Persona selection (guided login)

DECISION Managed persona selection and creating a persona in an open slot are `scope: now`
(`flow.md`, Pre-game). The landmarks below are what the client drives the shell by. Persona
deletion is `scope: later`.
source: director

| Shell state | Landmark (whitespace-collapsed, case-insensitive) | Guided login sends |
|-------------|---------------------------------------------------|--------------------|
| banner start | `logged in on` (`<account> logged in on <tty>.`) | - |
| banner end | `checking mail` (`[Checking mail...]`) | - |
| reset number | `This reset is number N.` (banner only) | - |
| yes/no question | `(y/n)` | `y` |
| top menu | `option (h for help)` | `p` to play |
| leftover input | `option unavailable` (a bare CR draws it, and so does a probe held unterminated in the shell buffer) | `0D 0A` first, to flush |
| persona list | `personae available to you are:` ... `by what name` ; entries split on `,` `.`, `(n)` stripped, `**Unused**` = free slot | persona name |
| name refused | `what shall i call you instead` | another name, or `q` |
| backing out | at the name prompt, `q` returns to the Option menu. At the Option menu itself, `q` means QUIT. | `q` |
| new persona | `creating new persona`, then `what sex do you wish to be` | `m` / `f` / `q` |
| examine sub-shell | `examine>`; entries `<name> Score: n Played: n male/female` | `e` |
| reset rebuild | `the database is still initialising` / `has started initialising` / `has finished initialising` | wait |
source: reference

## 4. Terminal width

| Item | Value | Scope | Source |
|------|-------|-------|--------|
| Fewest columns ever told | 20 | now | reference |
| Most columns ever told | 160 | now | reference |
| Fits | the number of whole character cells that fit across the available space (window or device width) at the current font and font size. It is computed before connecting, from the profile's font, so it is never unknown | now | director |
| Max-columns setting | unset (the default until the user sets it) or a ceiling, 20..160 | now | director |
| Effective columns | unset: min(80, fits). Set: min(setting, fits). Then clamped 20..160. | now | director |
| Columns told at connect | computed from the connect page width and the profile font, before the game screen exists | now | reference |
| Rows told | 21 | now | reference; clio:src/telnet.l {IAC}{DO}. |

### 4.1 On resize

| Trigger | Sent | Source |
|---------|------|--------|
| Effective column count changes (window resize, rotation, font size change, max-columns change) | NAWS SB with the new width x 21, if NAWS was agreed. Then, in game mode only, `1B 2D 5B 2F 54` digits `1B 2D 5D` (`$-[/T{cols}$-]`). | reference |
| Max-columns setting changed to a non-zero value | `$-[/T{value}$-]`, at once, whether or not in game mode | reference |
| Width unchanged | nothing | reference |

FACT The reference treats the `/T` width, not the NAWS width, as the one the server wraps game text
at, and expects a resize that sends only NAWS to leave new text wrapped at the old width.
source: reference
open: Resize outside game mode. After client-mode entry but at the Option menu, a resize sends NAWS
only. Changing max-columns, though, sends `/T` even at the shell. Should a shell-time resize also
send `/T`?
default: no. A shell-time resize sends NAWS only, as the reference does.

## 5. Probes, keepalive and timers

### 5.1 Probe format

FACT Bartle's probe form wraps the command in the ESC-[ ... ESC-] interrupt, which runs FES without
echoing it. Bytes: `1B 2D 5B 46 45 53 1B 2D 5D`.
source: bartle:FE4 2)

EVIDENCE None of FES, FEI, FEW and FEX produces output while the player is asleep, so periodic
sending does not wake them. FEW is snooped; the others are not.
source: bartle:FE4 FES/FEI/FEW/FEX

| Probe | Bytes | Sent by |
|-------|-------|---------|
| FES only | `1B 2D 5B` `FES` `1B 2D 5D` | Clio (all probes); reference (reset-time sampling) |
| Routine beat | `1B 2D 5B` `FES[,FEW][,FEI]` `1B 2D 5D`: comma-joined, FES always first | reference |
| Inventory | `1B 2D 5B` `FES,FEI` `1B 2D 5D` | reference |
| Exits | `1B 2D 5B` `FEX` `1B 2D 5D` | reference |
source: clio:src/txfes.c; reference

EVIDENCE Bartle never shows more than one command inside one interrupt.
source: bartle:FE4 2)
open: Comma batching inside one interrupt (`FES,FEW,FEI`). The reference relies on it and states
that FEW/FEI are unreliable without FES alongside. Nothing in Bartle or the captures confirms
either claim.
default: send the reference's comma-joined block, FES first (`$-[FES,FEW$-]`), the form `session.md`
uses. Bartle does not forbid it. The raw console can test it.

### 5.2 Cadence

| Probe | When | Default | Scope | Source |
|-------|------|---------|-------|--------|
| Routine beat | in game mode only. First beat at game entry is the full `FES,FEW,FEI`, then every N s. | N = 10 s. Setting: 0 = off; the dialog steps 5 to 30 in 5s; clamped 0..30. ini key `statupdate`. | now (FES); later (FEW, FEI: side panels) | reference |
| Reactive (stale-stats) | a C1 hint marks stats stale. A probe fires 200 ms later unless a text update or an FES arrives first. It is skipped if the routine beat is about to fire. | 200 ms delay | now | reference |
| Spacing | at least 500 ms between any two probes, routine or reactive | 500 ms | now | reference |
| Wake recovery | if an FES-carrying probe has gone unanswered for more than 5 s and any server bytes arrive, the beat fires at once (asleep, then woken). At most one per 2 s. | 5 s / 2 s | now | reference |
| Hold | probes can be suspended, for example while the director runs raw-console experiments. A held beat is delayed one full interval, never dropped. | - | now | reference; director (the raw console is `scope: now` in the Windows GUI and the TUI, `INTERNAL.md`) |
| FEW and FEI in the beat, inventory (combat), room-entry FEX, reset-time sampling | - | - | later: `later/probe-cadence.md` | reference |

DECISION No departed-player probe (`fair-play.md`, Other players). Rejected: the reference's
`value <name>` sent in front of a routine beat to test a player who left the Online list.
source: director

A probe block costs the player a command slot: `session.md`, Command understanding.

### 5.3 Setup batch on game entry

RULE The setup batch (DECISION below) goes out in one write,
`identify\r\nfightbrief\r\nauto fex\r\nscore\r\n`, then `1B 2D 5B 46 45 58 1B 2D 5D` (FEX). MuckaII
hides its echo and its replies from the terminal, including a high-ranking persona's refusal of
`auto fex` ("At your level, you should be well past the stage of needing to use AUTO FEEXITS
commands..."). The `score` reply supplies the persona name.
enforced_by: none: judgement
source: reference

EVIDENCE The server echoes the batch as one record: `identify\r\nfightbrief\r\nauto fex\r\nscore\r\n`.
The replies follow in their own frames: "You're already getting object identification numbers where
applicable.", "You're already getting brief descriptions of fights.", "You will now get an automatic
FEEXITS command performed every time you issue a movement command. To cancel it, use UNAUTO
FEEXITS.", then the score sheet starting `name:`.
source: capture:phone-login-at-20-columns.c1

DECISION MuckaII sends the setup batch automatically at each game-mode entry, as the reference does:
`identify`, `fightbrief`, `auto fex`, `score`, then `$-[FEX$-]`. It is part of arrival in the tea
room (`fair-play.md`, Automation); arrival is complete once it has been sent. `scope: now`.
Rejected: sending only `score`, or asking the player first.
source: director

### 5.4 Dreamword sends

| Action | Sent | Scope | Source |
|--------|------|-------|--------|
| Speak dreamword | `"` word `"` `0D 0A` | now | reference |
| Speak then follow up | `"` word `",` follow `0D 0A`. With an empty command box the follow-up is `sleep`. One leading comma is stripped. Input starting `,,` is refused. | now | reference |

RULE MuckaII keeps the dreamword across a reconnect into the same world, keyed on host, port and the
banner's `This reset is number N.`. Banner text is re-read on every login. A Reset drop (1.2) or a
new reset number clears it.
enforced_by: none: judgement
source: reference

### 5.5 Anti-idle keepalive

DECISION Anti-idle keepalive (`later/anti-idle.md`) and keep-screen-on (`later/keep-screen-on.md`)
are `scope: later`.
source: director

## 6. Command-line arguments

FACT A flag is `-name` (any number of leading `-`, case-insensitive) followed by its value. A
missing value, an unknown flag, or a value that is itself a known flag shows the error on the
connect page, and nothing connects. Words that are not flags are ignored.
source: reference

| Flag | Value | Effect | Scope | Source |
|------|-------|--------|-------|--------|
| `-profile` | name | select the saved profile (case-insensitive). If there is no match, the first (most recent) profile is used without warning. | now | reference |
| `-host` | host | override the host | now | reference |
| `-port` | 1..65535 | override the port. Out of range or not a number: ignored without warning. | now | reference |
| `-user` | name | override the telnet login name | now | reference |
| `-account` | id | override the account ID | now | reference |
| `-password` | text | override the password | now | reference |
| `-record` | (none) | arm the session transcript when the game is entered | later | reference |
| `-logs` | path | Windows only: append trace output to the file | later | reference |

FACT Any of `-profile -host -port -user -account -password` selects direct-connect mode. The app
connects once at startup without the user pressing Connect, does not save the profile, settings or
password, and closes when the server drops the connection.
source: reference

FACT A value given with `-password` or `-account` is visible to other users of the machine in
process listings, and in the shell's history.
source: reference

open: `-password` and `-account` on the command line: keep, drop, or replace with a safer route?
default: keep both flags, and say in the help text that their values are visible in process
listings and shell history.
