#!/usr/bin/env python3
"""Every repo text file contains only TAB, LF, CR and printable 7-bit bytes (0x09 0x0A 0x0D 0x20-0x7E).

Usage: check-text-files.py

Enforces the CLAUDE.md rule that the repo's own text files are 7-bit. Files checked: tracked plus
untracked-not-ignored (scripts/repofiles.py). Exempt by extension, because they are not our text:
  .c1   captures, the server's bytes verbatim
  .wav  .ttf   binary assets
SVG is text we ship and is checked like any other file. Any other file is checked; a binary file
with an unlisted extension therefore fails, and its extension must be added here on purpose.
Exits 1 on any violation, printing file:line for each (at most 5 lines per file).
"""

import os
import sys

sys.dont_write_bytecode = True  # __pycache__ is not gitignored and would fail this check
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import repofiles  # noqa: E402

EXEMPT = (".c1", ".wav", ".ttf")
ALLOWED = frozenset([0x09, 0x0A, 0x0D] + list(range(0x20, 0x7F)))
PER_FILE = 5


def main(argv):
    if argv:
        print(__doc__)
        return 2
    errors = []
    checked = 0
    for p in repofiles.files():
        if p.lower().endswith(EXEMPT):
            continue
        checked += 1
        data = repofiles.read(p)
        if all(b in ALLOWED for b in data):
            continue
        shown = 0
        for n, line in enumerate(data.split(b"\n"), 1):
            bad = [b for b in line if b not in ALLOWED]
            if bad:
                col = next(i for i, b in enumerate(line) if b not in ALLOWED) + 1
                errors.append("%s:%d:%d: byte 0x%02X not 7-bit printable (%d on line)"
                              % (p, n, col, bad[0], len(bad)))
                shown += 1
                if shown >= PER_FILE:
                    errors.append("%s: more lines not shown" % p)
                    break
    for e in errors:
        print(e)
    if errors:
        print("FAIL: %d violation(s)" % len(errors))
        return 1
    print("OK: %d text file(s) are 7-bit" % checked)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
