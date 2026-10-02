#!/usr/bin/env python3
"""Check statement tags in the spec: every statement has `source:`, every RULE has `enforced_by:`,
and every `open:` item has a `default:`.

Usage: check-spec-tags.py [--warn-defaults] [FILE.md ...]

The heuristic, exactly:
  1. Lines inside ``` fences are skipped entirely: they are neither tags nor tag lines.
  2. A statement starts at a line beginning `FACT `, `RULE `, `DECISION ` or `EVIDENCE ` (or the bare
     word alone on the line).
  3. A statement's span runs from its start line up to, not including, the first later line that is
     another statement start, a Markdown heading (`#`), or an `open:` line; or to end of file.
     Blank lines do NOT end a span, because a statement may carry a table or a second paragraph
     before its tag lines (events.md, the priming RULE). Table rows (`|`) are part of the span.
  4. The span must contain a line beginning `source:`, or a table that sources every row: its
     header has a `Capture` or `Source` column (any case) and that cell of every body row begins
     with a source token (`capture:`, `bartle:`, `clio:`, `director`, `reference`). A RULE's span
     must also contain a line beginning `enforced_by:`.
  5. An `open:` item's paragraph runs from the `open:` line to the next blank line, heading,
     statement start, or end of file. It must contain a line beginning `default:`.
Rule 5 fails like the others. --warn-defaults turns it back into a warning for a work in progress
(--strict-defaults is accepted and is the default). Exits 1 on any failure, printing file:line for
each.

With no files, checks docs/spec/*.md, docs/spec/later/*.md and every README.md under docs/evidence.
"""

import glob
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_GLOBS = [
    os.path.join(ROOT, "docs", "spec", "*.md"),
    os.path.join(ROOT, "docs", "spec", "later", "*.md"),
    os.path.join(ROOT, "docs", "evidence", "**", "README.md"),
]
TAG = re.compile(r"^(FACT|RULE|DECISION|EVIDENCE)( |$)")


def rel(path):
    try:
        return os.path.relpath(path)
    except ValueError:
        return path


SOURCE_TOKEN = re.compile(r"^`?(capture:|bartle:|clio:|director\b|reference\b)")


def cells(row):
    return [c.strip() for c in row.strip().strip("|").split("|")]


def table_sources_rows(span):
    """True if span holds a table with a Capture/Source column filled with a source on every row."""
    i = 0
    while i < len(span):
        if span[i].startswith("|") and i + 1 < len(span) and span[i + 1].startswith("|-"):
            head = [c.lower() for c in cells(span[i])]
            col = next((n for n, c in enumerate(head) if c in ("capture", "source")), None)
            j = i + 2
            rows = []
            while j < len(span) and span[j].startswith("|"):
                rows.append(cells(span[j]))
                j += 1
            if col is not None and rows and all(len(r) > col and SOURCE_TOKEN.match(r[col]) for r in rows):
                return True
            i = j
        else:
            i += 1
    return False


def check_file(path, errors, warnings):
    lines = open(path, "rb").read().decode("ascii", "replace").split("\n")
    kinds = []  # per line: 'fence', 'tag', 'head', 'open', 'blank', or 'text'
    fenced = False
    for line in lines:
        if line.startswith("```"):
            fenced = not fenced
            kinds.append("fence")
        elif fenced:
            kinds.append("fence")
        elif TAG.match(line):
            kinds.append("tag")
        elif line.startswith("#"):
            kinds.append("head")
        elif line.startswith("open:"):
            kinds.append("open")
        elif line.strip() == "":
            kinds.append("blank")
        else:
            kinds.append("text")
    name = rel(path)
    count = 0
    for i, k in enumerate(kinds):
        if k == "tag":
            count += 1
            j = i + 1
            while j < len(lines) and kinds[j] not in ("tag", "head", "open"):
                j += 1
            span = [lines[x] for x in range(i + 1, j) if kinds[x] != "fence"]
            tag = TAG.match(lines[i]).group(1)
            if not any(s.startswith("source:") for s in span) and not table_sources_rows(span):
                errors.append("%s:%d: %s without a source: line" % (name, i + 1, tag))
            if tag == "RULE" and not any(s.startswith("enforced_by:") for s in span):
                errors.append("%s:%d: RULE without an enforced_by: line" % (name, i + 1))
        elif k == "open":
            j = i + 1
            while j < len(lines) and kinds[j] in ("text",):
                j += 1
            if not any(lines[x].startswith("default:") for x in range(i + 1, j)):
                warnings.append("%s:%d: open: without a default: line" % (name, i + 1))
    return count


def main(argv):
    strict = True
    paths = []
    for a in argv:
        if a == "--strict-defaults":
            strict = True
        elif a == "--warn-defaults":
            strict = False
        elif a.startswith("-"):
            print(__doc__)
            return 2
        else:
            paths.append(a)
    if not paths:
        for pattern in DEFAULT_GLOBS:
            paths += glob.glob(pattern, recursive=True)
        paths = sorted(set(paths))
    errors, warnings = [], []
    total = 0
    for p in paths:
        total += check_file(p, errors, warnings)
    if strict:
        errors += warnings
        warnings = []
    for w in warnings:
        print("warning: " + w)
    for e in errors:
        print(e)
    if errors:
        print("FAIL: %d violation(s)" % len(errors))
        return 1
    print("OK: %d statement(s) in %d file(s) tagged%s" % (
        total, len(paths), "; %d warning(s) (open: without default:)" % len(warnings) if warnings else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
