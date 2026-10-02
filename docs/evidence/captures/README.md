# Captures

FACT Each `.c1` file here is a capture of live MUD2 wire traffic: the server's bytes, verbatim, one
record per line. The captures and the wire corpus are the only safe place to experiment, because
there is no test server.
source: director

## Line grammar

FACT A capture is a sequence of records. Every record is one line, and every line, the last
included, ends in LF (0x0A):

```
record    = ts_ms SP direction SP payload LF
ts_ms     = 1*DIGIT                 ; milliseconds, decimal ASCII
direction = "rx" / "tx" / "an"
payload   = *( raw-byte / escape )
escape    = %x5C %x5C  /  %x5C "r"  /  %x5C "n"   ; backslash then \, r or n
raw-byte  = any byte except LF (0x0A), CR (0x0D) and backslash (0x5C)
SP        = 0x20, exactly one
```

`rx` is bytes received from the server, `tx` bytes sent to it, `an` an annotation. The payload
starts after the second space and runs to the LF; it is not trimmed, so a payload may begin with a
space.
source: director

FACT The payload is the record's raw bytes. Read it as Latin-1, so that each byte value equals the
character value, then undo exactly three escapes: backslash-backslash is one backslash (0x5C),
backslash-r is CR (0x0D), backslash-n is LF (0x0A). There are no others. Raw NUL (0x00) and bytes
0x80-0xFF appear unescaped, as themselves.
source: director

RULE A capture parses only under this grammar. A raw CR, a backslash followed by anything other than
`\`, `r` or `n`, a backslash at the end of a payload, an empty line, a missing final LF, a timestamp
that is not decimal digits, or a direction other than `rx`, `tx`, `an` is a violation.
enforced_by: scripts/check-captures.py
source: director

RULE Within a capture, timestamps never decrease from one record to the next.
enforced_by: scripts/check-captures.py
source: capture:blind-vs-water-snake.c1
source: capture:dark-two-rats.c1
source: capture:invisible-man-preemptive.c1
source: capture:invisible-player-joins.c1
source: capture:phone-login-at-20-columns.c1
source: capture:wyvern-poison-death.c1

RULE Open a capture in binary mode and read bytes. A UTF-8 text reader corrupts it: bytes 0x80-0xFF
are not valid UTF-8 on their own, and they are MUD2 markup, not characters. Newline translation
corrupts it too. `scripts/c1.py` is the reader the repo's scripts use.
enforced_by: none: judgement
source: director

RULE The golden streams count `rx` records only, from zero, in file order (`docs/spec/events.md`
"Driving the decoder"). `tx` and `an` records are not fed to the decoder and take no ordinal.
enforced_by: scripts/check-goldens.py
source: director

## What the six captures hold

EVIDENCE Measured across all six captures: every record is `rx`; there are no `tx` or `an`
records. The only escapes that occur are `\r` and `\n`; `\\` never occurs. Raw NUL occurs in every
capture (the `\r\0\r\n` line end), and so do bytes 0x9B-0xFF. No raw CR occurs. Every timestamp is
13 digits, and timestamps never decrease within a capture.
source: capture:blind-vs-water-snake.c1
source: capture:dark-two-rats.c1
source: capture:invisible-man-preemptive.c1
source: capture:invisible-player-joins.c1
source: capture:phone-login-at-20-columns.c1
source: capture:wyvern-poison-death.c1

| Capture | Records |
|---------|---------|
| blind-vs-water-snake.c1 | 40 |
| dark-two-rats.c1 | 58 |
| invisible-man-preemptive.c1 | 26 |
| invisible-player-joins.c1 | 55 |
| phone-login-at-20-columns.c1 | 7 |
| wyvern-poison-death.c1 | 6 |

## Immutability

RULE Captures are evidence and byte-immutable. Never edit, re-save, normalise line ends, or re-encode
one. A new observation is a new file.
enforced_by: scripts/check-captures.py
source: director

FACT `SHA256SUMS` holds the SHA-256 of every capture, one `<hex> *<name>` line each. The check fails
if a capture's hash differs, a listed capture is missing, or a `.c1` here is not listed.
`.gitattributes` marks `*.c1` binary, so git applies no line-end conversion.
source: director
