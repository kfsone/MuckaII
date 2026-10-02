"""Binary-safe reader for `.c1` capture files (grammar: docs/evidence/captures/README.md).

A file is a sequence of records, one per LF-terminated line:

    <ts_ms> SP <rx|tx|an> SP <payload> LF

`ts_ms` is one or more ASCII digits. `payload` is raw bytes in which a backslash starts one of
exactly three escapes: `\\\\` (backslash), `\\r` (CR), `\\n` (LF). A raw CR, a raw LF inside a
payload, or any other backslash sequence is a grammar violation. Every other byte value, including
NUL and 0x80-0xFF, stands for itself.

Never open a capture in text mode. This module reads bytes only.
"""

import collections

DIRECTIONS = (b"rx", b"tx", b"an")

Record = collections.namedtuple("Record", "line ts direction payload")
"""line: 1-based line number. ts: int ms. direction: 'rx' | 'tx' | 'an'. payload: unescaped bytes."""


class C1Error(ValueError):
    def __init__(self, line, msg):
        ValueError.__init__(self, "line %d: %s" % (line, msg))
        self.line = line
        self.msg = msg


def unescape(raw, line=0):
    """Undo the three capture escapes in one payload. Raises C1Error on anything else."""
    out = bytearray()
    i = 0
    n = len(raw)
    while i < n:
        b = raw[i]
        if b == 0x5C:
            if i + 1 >= n:
                raise C1Error(line, "lone backslash at end of payload (byte %d)" % i)
            c = raw[i + 1]
            if c == 0x5C:
                out.append(0x5C)
            elif c == 0x72:
                out.append(0x0D)
            elif c == 0x6E:
                out.append(0x0A)
            else:
                raise C1Error(line, "unknown escape \\x%02X after backslash (byte %d)" % (c, i))
            i += 2
            continue
        if b == 0x0D:
            raise C1Error(line, "raw CR in payload (byte %d); CR must be written \\r" % i)
        out.append(b)
        i += 1
    return bytes(out)


def parse_bytes(data):
    """Parse a whole capture's bytes. Returns a list of Record. Raises C1Error on the first violation."""
    if not data:
        raise C1Error(1, "empty file")
    if not data.endswith(b"\n"):
        raise C1Error(data.count(b"\n") + 1, "last record has no LF")
    records = []
    for n, line in enumerate(data[:-1].split(b"\n"), 1):
        if not line:
            raise C1Error(n, "empty line")
        sp1 = line.find(b" ")
        if sp1 < 0:
            raise C1Error(n, "no space after timestamp")
        ts = line[:sp1]
        if not ts or not all(0x30 <= b <= 0x39 for b in ts):
            raise C1Error(n, "timestamp %r is not decimal digits" % ts[:20])
        sp2 = line.find(b" ", sp1 + 1)
        if sp2 < 0:
            raise C1Error(n, "no space after direction")
        direction = line[sp1 + 1:sp2]
        if direction not in DIRECTIONS:
            raise C1Error(n, "direction %r is not rx, tx or an" % direction[:20])
        payload = unescape(line[sp2 + 1:], n)
        records.append(Record(n, int(ts), direction.decode("ascii"), payload))
    return records


def read(path):
    """Parse the capture at path (opened in binary mode)."""
    with open(path, "rb") as f:
        return parse_bytes(f.read())


def rx_count(path):
    """Number of rx records: golden `rx` ordinals are 0 .. rx_count - 1."""
    return sum(1 for r in read(path) if r.direction == "rx")
