#!/usr/bin/env python3
"""Verify the capture evidence: SHA256SUMS and the .c1 line grammar.

Usage: check-captures.py [--dir DIR]

DIR defaults to docs/evidence/captures. Checks, all reading bytes only:
  - SHA256SUMS lines are `<64 hex> *<name>` (or two spaces in place of ` *`), one name per line;
  - every listed file exists and its SHA-256 matches;
  - every *.c1 in DIR is listed;
  - every *.c1 parses under the grammar in scripts/c1.py (docs/evidence/captures/README.md);
  - timestamps never decrease within a capture.
Exits 1 on any violation, printing file:line for each.
"""

import glob
import hashlib
import os
import re
import sys

sys.dont_write_bytecode = True  # __pycache__ is not gitignored and would fail check-text-files
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_DIR = os.path.join(ROOT, "docs", "evidence", "captures")
SUM_LINE = re.compile(rb"^([0-9a-f]{64}) [ *]([^/\\]+)$")


def rel(path):
    try:
        return os.path.relpath(path)
    except ValueError:
        return path


def main(argv):
    d = DEFAULT_DIR
    if argv[:1] == ["--dir"] and len(argv) == 2:
        d = argv[1]
    elif argv:
        print(__doc__)
        return 2
    errors = []
    sums_path = os.path.join(d, "SHA256SUMS")
    listed = {}
    if not os.path.isfile(sums_path):
        errors.append("%s:1: SHA256SUMS missing" % rel(sums_path))
    else:
        raw = open(sums_path, "rb").read()
        if raw and not raw.endswith(b"\n"):
            errors.append("%s:%d: last line has no LF" % (rel(sums_path), raw.count(b"\n") + 1))
        for n, line in enumerate(raw.split(b"\n"), 1):
            if not line:
                continue
            m = SUM_LINE.match(line)
            if not m:
                errors.append("%s:%d: not '<sha256> *<name>'" % (rel(sums_path), n))
                continue
            name = m.group(2).decode("ascii", "replace")
            if name in listed:
                errors.append("%s:%d: %s listed twice" % (rel(sums_path), n, name))
            listed[name] = (n, m.group(1).decode("ascii"))
    for name, (n, want) in sorted(listed.items()):
        p = os.path.join(d, name)
        if not os.path.isfile(p):
            errors.append("%s:%d: %s listed but missing" % (rel(sums_path), n, name))
            continue
        got = hashlib.sha256(open(p, "rb").read()).hexdigest()
        if got != want:
            errors.append("%s:1: SHA-256 %s, SHA256SUMS line %d says %s" % (rel(p), got, n, want))
    captures = sorted(glob.glob(os.path.join(d, "*.c1")))
    if not captures:
        errors.append("%s: no .c1 files" % rel(d))
    nrec = 0
    for p in captures:
        name = os.path.basename(p)
        if name not in listed:
            errors.append("%s:1: not listed in SHA256SUMS" % rel(p))
        try:
            recs = c1.read(p)
        except c1.C1Error as e:
            errors.append("%s:%d: %s" % (rel(p), e.line, e.msg))
            continue
        nrec += len(recs)
        for a, b in zip(recs, recs[1:]):
            if b.ts < a.ts:
                errors.append("%s:%d: timestamp %d before previous %d" % (rel(p), b.line, b.ts, a.ts))
    for e in errors:
        print(e)
    if errors:
        print("FAIL: %d violation(s)" % len(errors))
        return 1
    print("OK: %d capture(s), %d record(s), SHA256SUMS matches" % (len(captures), nrec))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
