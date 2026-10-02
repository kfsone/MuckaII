#!/usr/bin/env python3
"""Firewall tripwire: no reference identifier appears anywhere in the tree.

Usage: check-tripwire.py

Reads scripts/firewall/reference-identifiers.txt (one PascalCase name per line). For each name it
searches three forms: the name as written, its camelCase form, and its snake_case form. Words are
split at lower/digit-to-upper and at upper-to-upper-lower boundaries, so a leading acronym stays one
word (`IFooBar` -> `i_foo_bar`, `iFooBar`). Matching is case-sensitive and whole-word: the
character before and after must not be a letter, digit or underscore.

Files searched: tracked plus untracked-not-ignored (scripts/repofiles.py), every one read as bytes,
binary assets included. Exempt: the identifier list itself, and *.c1 (server bytes, not ours).
Exits 1 on any hit, printing file:line for each.
"""

import os
import re
import sys

sys.dont_write_bytecode = True  # __pycache__ is not gitignored and would fail check-text-files
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import repofiles  # noqa: E402

LIST = "scripts/firewall/reference-identifiers.txt"


def words(name):
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1 \2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", s)
    return s.split()


def variants(name):
    w = words(name)
    snake = "_".join(x.lower() for x in w)
    camel = w[0].lower() + "".join(w[1:])
    return sorted(set([name, camel, snake]))


def load():
    names = []
    for line in repofiles.read(LIST).decode("ascii").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            names.append(line)
    return names


def main(argv):
    if argv:
        print(__doc__)
        return 2
    names = load()
    forms = {}
    for n in names:
        for v in variants(n):
            forms.setdefault(v, n)
    alt = "|".join(re.escape(v) for v in sorted(forms, key=len, reverse=True))
    pat = re.compile(r"(?<![A-Za-z0-9_])(" + alt + r")(?![A-Za-z0-9_])")
    errors = []
    checked = 0
    for p in repofiles.files():
        if p == LIST or p.lower().endswith(".c1"):
            continue
        checked += 1
        text = repofiles.read(p).decode("latin-1")
        for m in pat.finditer(text):
            ln = text.count("\n", 0, m.start()) + 1
            hit = m.group(1)
            errors.append("%s:%d: reference identifier %r%s" % (
                p, ln, hit, "" if hit == forms[hit] else " (form of %s)" % forms[hit]))
    for e in errors:
        print(e)
    if errors:
        print("FAIL: %d hit(s)" % len(errors))
        return 1
    print("OK: %d file(s) clean of %d identifier(s) in %d form(s)" % (checked, len(names), len(forms)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
