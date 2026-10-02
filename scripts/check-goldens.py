#!/usr/bin/env python3
"""Validate golden event streams against the schema block in docs/spec/events.md.

Usage: check-goldens.py [--events PATH] [GOLDEN.jsonl ...]
       check-goldens.py [--events PATH] --self-test

With no golden paths, checks docs/evidence/golden/*.jsonl. Also checks that the prose event
tables in events.md name the same events and keys as the schema block. Exits 1 on any violation,
printing file:line for each.

Each golden is cross-checked against its capture: the stream record's `source` names
`capture:<file>.c1` in docs/evidence/captures, that file must exist, and every `rx` must be below
the capture's count of rx records (rx ordinals are zero-based). A golden in docs/evidence/golden
must also be named after its capture (`<file>.jsonl`).

--self-test proves the checker can fail. Every fixture in scripts/fixtures/golden-must-fail/ must
fail, and every one of its violations must contain the substring EXPECT.txt gives for it. Further
must-fail cases that cannot live in the tree as text (non-ASCII byte, CR, no final LF, blank line)
are generated in a temp directory. The fixture directory and EXPECT.txt must list the same files.
The real goldens must pass.
"""

import glob
import json
import os
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True  # __pycache__ is not gitignored and would fail check-text-files
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
DEFAULT_EVENTS = os.path.join(ROOT, "docs", "spec", "events.md")
GOLDEN_DIR = os.path.join(ROOT, "docs", "evidence", "golden")
DEFAULT_GOLDENS = os.path.join(GOLDEN_DIR, "*.jsonl")
CAPTURE_DIR = os.path.join(ROOT, "docs", "evidence", "captures")
MUST_FAIL_DIR = os.path.join(ROOT, "scripts", "fixtures", "golden-must-fail")
_rx_counts = {}
NAME = re.compile(r"^[a-z][a-z_]*$")

errors = []


def rel(path):
    try:
        return os.path.relpath(path)
    except ValueError:
        return path


def err(where, msg):
    errors.append("%s: %s" % (where, msg))


# -- schema block ------------------------------------------------------------------------------

def load_schema(path):
    text = open(path, "rb").read().decode("ascii")
    m = re.search(r"## Machine-readable schema.*?```json\n(.*?)\n```", text, re.S)
    if not m:
        err("%s:1" % path, "no ```json block under '## Machine-readable schema'")
        return None, text
    start = text[:m.start(1)].count("\n") + 1
    try:
        return json.loads(m.group(1)), text
    except ValueError as e:
        err("%s:%d" % (path, start), "schema block is not JSON: %s" % e)
        return None, text


def keylists(schema, ev):
    """All key lists an event may use (stats has one per source)."""
    spec = schema["events"][ev]
    if isinstance(spec, dict):
        return spec
    return {None: spec}


# -- prose agreement ---------------------------------------------------------------------------

def table_rows(text, header_prefix):
    """Yield (line_no, cells) for every body row of every table whose header starts as given."""
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        if lines[i].startswith(header_prefix):
            j = i + 2
            while j < len(lines) and lines[j].startswith("|"):
                cells = [c.strip() for c in lines[j].strip().strip("|").split("|")]
                yield j + 1, cells
                j += 1
            i = j
        else:
            i += 1


def ticks(cell):
    return [t for t in re.findall(r"`([^`]*)`", cell) if NAME.match(t) and t != "null"]


def line_of(text, needle, after=0):
    """1-based number of the first line at or after line `after` that contains needle (else 1)."""
    for n, line in enumerate(text.split("\n"), 1):
        if n >= after and needle in line:
            return n
    return 1


def check_prose(path, schema, text):
    fence = line_of(text, "```json")
    prose = {}
    for ln, cells in table_rows(text, "| `ev` |"):
        names = ticks(cells[0])
        if len(names) != 1:
            err("%s:%d" % (path, ln), "event row without one event name")
            continue
        prose[names[0]] = (ln, ticks(cells[1]))
    block = dict(schema["events"])
    block.update(schema["wire"])
    for ev in sorted(set(prose) - set(block)):
        err("%s:%d" % (path, prose[ev][0]), "prose table names event %r, schema block does not" % ev)
    for ev in sorted(set(block) - set(prose)):
        err("%s:%d" % (path, line_of(text, '"%s":' % ev, fence)),
            "schema block has event %r, no prose table row names it" % ev)
    stat_rows = [ticks(c[0]) for _, c in table_rows(text, "| Key | Type | FES field |")]
    stat_keys = [r[0] for r in stat_rows if r]
    for ev, (ln, keys) in sorted(prose.items()):
        if ev not in block:
            continue
        spec = block[ev]
        if isinstance(spec, dict):
            want_fes = [k for k, _ in spec["fes"]]
            if keys + stat_keys != want_fes:
                err("%s:%d" % (path, ln), "%s keys in prose (%s + stat table) differ from block fes list"
                    % (ev, ",".join(keys)))
            code = [k for k, _ in spec["code"]]
            if code[0] != "source" or any(k not in want_fes for k in code):
                err("%s:%d" % (path, line_of(text, '"code": [', fence)),
                    "stats code key list is not source plus fes keys")
        elif keys != [k for k, _ in spec]:
            err("%s:%d" % (path, ln), "%s keys in prose %s differ from block %s"
                % (ev, keys, [k for k, _ in spec]))
    style_rows = [ticks(c[0])[0] for _, c in table_rows(text, "| Key | Type | Meaning |") if ticks(c[0])]
    if style_rows != [k for k, _ in schema["types"]["style"]]:
        err("%s:%d" % (path, line_of(text, "| Key | Type | Meaning |")),
            "style table keys %s differ from block %s" % (style_rows, [k for k, _ in schema["types"]["style"]]))


# -- value types -------------------------------------------------------------------------------

def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def check_type(schema, t, v, where, what):
    if isinstance(t, dict):
        if "nullable" in t:
            if v is not None:
                check_type(schema, t["nullable"], v, where, what)
            return
        if "array" in t:
            if not isinstance(v, list) or isinstance(v, Obj):
                err(where, "%s: expected array, got %s" % (what, json.dumps(v)))
                return
            if "max" in t and len(v) > t["max"]:
                err(where, "%s: array longer than %d" % (what, t["max"]))
            for i, x in enumerate(v):
                check_type(schema, t["array"], x, where, "%s[%d]" % (what, i))
            return
        if "enum" in t:
            if v not in t["enum"] or not isinstance(v, str):
                err(where, "%s: %s not one of %s" % (what, json.dumps(v), t["enum"]))
            return
        err(where, "%s: unknown type %s in schema" % (what, t))
        return
    if t == "int":
        ok = is_int(v)
    elif t == "bool":
        ok = isinstance(v, bool)
    elif t == "string":
        ok = isinstance(v, str)
    elif t == "text":
        ok = isinstance(v, str) and len(v) > 0 and not any(c in v for c in "\r\n\0")
    elif t == "colour":
        ok = is_int(v) and 0 <= v <= 15
    elif t == "letter":
        ok = isinstance(v, str) and len(v) == 1
    elif t == "attrs":
        names = schema["attrs"]
        ok = (isinstance(v, list) and all(isinstance(x, str) and x in names for x in v)
              and v == sorted(set(v)))
    elif t in schema["types"]:
        check_object(schema, schema["types"][t], v, where, what)
        return
    else:
        err(where, "%s: unknown type %r in schema" % (what, t))
        return
    if not ok:
        err(where, "%s: expected %s, got %s" % (what, t, json.dumps(v)))


def check_object(schema, keylist, v, where, what):
    if not isinstance(v, Obj):
        err(where, "%s: expected object, got %s" % (what, json.dumps(v)))
        return
    got = [k for k, _ in v]
    want = [k for k, _ in keylist]
    if got != want:
        err(where, "%s: keys %s, expected %s" % (what, got, want))
        return
    for (k, x), (_, t) in zip(v, keylist):
        check_type(schema, t, x, where, "%s.%s" % (what, k))


class Obj(list):
    """A JSON object kept as its (key, value) pairs, so key order and duplicates are visible."""


# -- goldens -----------------------------------------------------------------------------------

def check_golden(path, schema, name_check=None):
    """name_check: require the file to be named after its capture (default: when in GOLDEN_DIR)."""
    raw = open(path, "rb").read()
    name = rel(path)
    if any(b > 0x7F for b in raw):
        for n, line in enumerate(raw.split(b"\n"), 1):
            if any(b > 0x7F for b in line):
                err("%s:%d" % (name, n), "non-ASCII byte")
    if b"\r" in raw:
        err("%s:%d" % (name, raw[:raw.index(b"\r")].count(b"\n") + 1), "CR in file; line ends must be LF")
    if raw and not raw.endswith(b"\n"):
        err("%s:%d" % (name, raw.count(b"\n") + 1), "last line has no LF")
        raw += b"\n"
    lines = raw.decode("ascii", "replace").split("\n")[:-1]
    if not lines:
        err("%s:1" % name, "empty file")
        return
    last_rx = None
    capture = None
    for n, line in enumerate(lines, 1):
        where = "%s:%d" % (name, n)
        if line == "":
            err(where, "blank line")
            continue
        try:
            rec = json.loads(line, object_pairs_hook=Obj)
        except ValueError as e:
            err(where, "not JSON: %s" % e)
            continue
        if not isinstance(rec, Obj) or len(rec) < 2 or rec[0][0] != "ev" or rec[1][0] != "rx":
            err(where, "record must be an object starting with ev, rx")
            continue
        keys = [k for k, _ in rec]
        if len(set(keys)) != len(keys):
            err(where, "duplicate key")
        ev, rx = rec[0][1], rec[1][1]
        if not isinstance(ev, str):
            err(where, "ev is not a string")
            continue
        if ev in schema.get("wire", {}):
            err(where, "wire-tier event %r in a golden" % ev)
            continue
        if ev not in schema["events"]:
            err(where, "unknown event %r" % ev)
            continue
        if not is_int(rx):
            err(where, "rx is not an integer")
            continue
        body = Obj(rec[2:])
        if n == 1:
            if ev != "stream" or rx != -1:
                err(where, "first record must be stream with rx -1")
        elif ev == "stream":
            err(where, "stream record after line 1")
        elif rx < 0:
            err(where, "rx %d is negative" % rx)
        if last_rx is not None and rx < last_rx:
            err(where, "rx %d after rx %d" % (rx, last_rx))
        last_rx = rx
        lists = keylists(schema, ev)
        if None in lists:
            keylist = lists[None]
        else:
            src = dict(body).get("source")
            if src not in lists:
                err(where, "%s source %s not one of %s" % (ev, json.dumps(src), sorted(lists)))
                continue
            keylist = lists[src]
        check_object(schema, keylist, body, where, ev)
        if ev == "stream" and dict(body).get("schema") != schema["schema"]:
            err(where, "stream schema %s, events.md is %s" % (dict(body).get("schema"), schema["schema"]))
        if ev == "stream" and n == 1:
            capture = capture_count(dict(body).get("source"), path, where, name_check)
        if n > 1 and capture is not None and rx >= capture[1]:
            err(where, "rx %d beyond the %d rx record(s) of %s" % (rx, capture[1], capture[0]))


def capture_count(source, path, where, name_check=None):
    """(name, rx record count) of the capture a stream record names, or None after reporting why."""
    m = re.match(r"^capture:([A-Za-z0-9._-]+\.c1)$", source if isinstance(source, str) else "")
    if not m:
        err(where, "stream source %s is not capture:<file>.c1" % json.dumps(source))
        return None
    name = m.group(1)
    if name_check is None:
        name_check = os.path.normcase(os.path.dirname(os.path.abspath(path))) == os.path.normcase(GOLDEN_DIR)
    if name_check:
        want = os.path.splitext(os.path.basename(path))[0] + ".c1"
        if name != want:
            err(where, "golden %s names capture %s; expected %s" % (os.path.basename(path), name, want))
    cpath = os.path.join(CAPTURE_DIR, name)
    if not os.path.isfile(cpath):
        err(where, "capture %s not found in %s" % (name, rel(CAPTURE_DIR)))
        return None
    if name not in _rx_counts:
        try:
            _rx_counts[name] = c1.rx_count(cpath)
        except c1.C1Error as e:
            err(where, "capture %s does not parse: %s" % (name, e))
            return None
    return name, _rx_counts[name]


# -- self-test ---------------------------------------------------------------------------------

def generated_cases(tmp):
    """Byte-level must-fail cases, built from a real golden so they cannot live in the tree as text."""
    base = open(os.path.join(GOLDEN_DIR, "wyvern-poison-death.jsonl"), "rb").read()
    lines = base.split(b"\n")[:6]
    body = b"\n".join(lines) + b"\n"
    cases = {
        "gen-non-ascii.jsonl": (body.replace(b"wyvern is", b"wyv\xe9rn is", 1), "non-ASCII byte"),
        "gen-cr.jsonl": (body.replace(b"\n", b"\r\n", 2), "CR in file"),
        "gen-no-final-lf.jsonl": (body[:-1], "last line has no LF"),
        "gen-blank-line.jsonl": (body.replace(b"\n", b"\n\n", 1), "blank line"),
        # Valid content under another capture's name; checked as if it sat in GOLDEN_DIR.
        "dark-two-rats.jsonl": (body, "names capture wyvern-poison-death.c1; expected dark-two-rats.c1"),
    }
    out = []
    for name, (data, want) in sorted(cases.items()):
        p = os.path.join(tmp, name)
        with open(p, "wb") as f:
            f.write(data)
        out.append((p, want, not name.startswith("gen-")))
    return out


def self_test(events):
    failures = []
    schema, text = load_schema(events)
    if schema is None:
        return ["schema block unreadable: %s" % errors], 0, 0
    del errors[:]
    real = sorted(glob.glob(DEFAULT_GOLDENS))
    if not real:
        failures.append("no real goldens found")
    check_prose(rel(events), schema, text)
    for p in real:
        check_golden(p, schema)
    for e in errors:
        failures.append("real golden should pass: %s" % e)
    expect_path = os.path.join(MUST_FAIL_DIR, "EXPECT.txt")
    expect = {}
    for line in open(expect_path, "rb").read().decode("ascii").splitlines():
        if line.strip() and not line.startswith("#"):
            name, _, want = line.partition(" ")
            expect[name] = want.strip()
    on_disk = set(os.path.basename(p) for p in glob.glob(os.path.join(MUST_FAIL_DIR, "*.jsonl")))
    for name in sorted(on_disk - set(expect)):
        failures.append("%s: fixture has no EXPECT.txt line" % name)
    for name in sorted(set(expect) - on_disk):
        failures.append("EXPECT.txt names %s, which does not exist" % name)
    cases = [(os.path.join(MUST_FAIL_DIR, n), expect[n], False) for n in sorted(on_disk & set(expect))]
    tmp = tempfile.mkdtemp(prefix="golden-must-fail-")
    try:
        cases += generated_cases(tmp)
        for p, want, name_check in cases:
            del errors[:]
            check_golden(p, schema, name_check)
            name = os.path.basename(p)
            if not errors:
                failures.append("%s: must fail, but passed" % name)
            elif not all(want in e for e in errors):
                failures.append("%s: must fail only with %r, got %s" % (name, want, errors))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    del errors[:]
    return failures, len(real), len(cases)


def main(argv):
    events = DEFAULT_EVENTS
    paths = []
    selftest = False
    i = 0
    while i < len(argv):
        if argv[i] == "--events":
            events = argv[i + 1]
            i += 2
            continue
        if argv[i] == "--self-test":
            selftest = True
            i += 1
            continue
        paths.append(argv[i])
        i += 1
    if selftest:
        failures, nreal, ncases = self_test(events)
        for f in failures:
            print(f)
        if failures:
            print("FAIL: self-test, %d problem(s)" % len(failures))
            return 1
        print("OK: self-test, %d real golden(s) pass, %d must-fail case(s) fail as expected" % (nreal, ncases))
        return 0
    if not paths:
        paths = sorted(glob.glob(DEFAULT_GOLDENS))
    if not paths:
        err("check-goldens", "no golden files found")
    schema, text = load_schema(events)
    if schema is not None:
        check_prose(rel(events), schema, text)
        for p in paths:
            check_golden(p, schema)
    for e in errors:
        print(e)
    if errors:
        print("FAIL: %d violation(s)" % len(errors))
        return 1
    print("OK: %d golden file(s) valid against %s" % (len(paths), rel(events)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
