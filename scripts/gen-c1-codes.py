#!/usr/bin/env python3
"""Generate the decoder's C1 code table from docs/spec/c1-codes.toml.

Usage: gen-c1-codes.py          write src/mud/codeTable.cpp
       gen-c1-codes.py --check  fail if src/mud/codeTable.cpp is not what this script writes

c1-codes.toml says the table generates the decoder's codes, so the C++ table is never edited by
hand. The output is run through clang-format (the pinned LLVM, scripts/llvmtools.py) so it passes
the format gate. The TOML is parsed here without tomllib: Python 3.10 is on the director's PATH and
has none. Every entry is one inline table on one line, which is all this parser accepts.
"""

import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import llvmtools  # noqa: E402
import repofiles  # noqa: E402

TOML = os.path.join(repofiles.ROOT, "docs", "spec", "c1-codes.toml")
OUT = os.path.join(repofiles.ROOT, "src", "mud", "codeTable.cpp")
OUT_REL = "src/mud/codeTable.cpp"

STACK_OPS = {"push": "kPush", "push_empty": "kPushEmpty", "catch": "kCatch", "throw": "kThrow"}
ROLES = [
    "", "prompt_container", "prompt_core", "prompt_indicator", "room_name", "room_long",
    "creature_sentence", "creature_invisible", "who_name", "presence_name", "fight_start",
    "fight_end", "persona_unsaved", "chat", "fes_line", "fe_exits", "fe_inventory", "fe_who",
    "dreamword", "dreamword_cleared", "stat_stamina", "stat_max_stamina", "stat_score",
    "snoop_prefix", "client_mode", "account_change", "account_logout",
]


class GenError(Exception):
    pass


def tokens(text, line):
    """Tokenise one inline table body: strings, integers, names, = , [ ]."""
    i = 0
    out = []
    while i < len(text):
        c = text[i]
        if c in " \t":
            i += 1
        elif c in "=,[]":
            out.append(c)
            i += 1
        elif c == '"':
            j = text.index('"', i + 1)
            if "\\" in text[i + 1:j]:
                raise GenError("line %d: escapes in strings are not supported" % line)
            out.append(("str", text[i + 1:j]))
            i = j + 1
        else:
            m = re.match(r"-?0x[0-9A-Fa-f]+|-?\d+|[a-z_]+", text[i:])
            if not m:
                raise GenError("line %d: cannot read %r" % (line, text[i:i + 20]))
            word = m.group(0)
            if re.match(r"-?0x", word):
                out.append(("int", int(word, 16)))
            elif re.match(r"-?\d", word):
                out.append(("int", int(word)))
            else:
                out.append(("name", word))
            i += len(word)
    return out


def parse_entry(body, line):
    toks = tokens(body, line)
    entry = {}
    i = 0
    while i < len(toks):
        key = toks[i]
        if not (isinstance(key, tuple) and key[0] == "name") or toks[i + 1] != "=":
            raise GenError("line %d: expected key = value" % line)
        i += 2
        if toks[i] == "[":
            values = []
            i += 1
            while toks[i] != "]":
                if toks[i] != ",":
                    values.append(toks[i][1])
                i += 1
            i += 1
            entry[key[1]] = values
        else:
            entry[key[1]] = toks[i][1]
            i += 1
        if i < len(toks):
            if toks[i] != ",":
                raise GenError("line %d: expected a comma after %s" % (line, key[1]))
            i += 1
    return entry


def read_entries():
    entries = []
    with open(TOML, "rb") as f:
        lines = f.read().decode("ascii").split("\n")
    for n, text in enumerate(lines, 1):
        m = re.match(r"^\s*\{(.*)\},?\s*$", text)
        if m:
            entries.append((n, parse_entry(m.group(1), n)))
    if not entries:
        raise GenError("no entries found in %s" % TOML)
    return entries


def camel(role):
    return "k" + "".join(w.capitalize() for w in role.split("_")) if role else "kNone"


def generate():
    rows = []
    seen = set()
    for line, e in read_entries():
        numbers = [int(x) for x in e["id"].split()]
        if not 1 <= len(numbers) <= 3 or any(not 0 <= x <= 99 for x in numbers):
            raise GenError("line %d: id %r is not 1-3 numbers 00-99" % (line, e["id"]))
        if e["id"] in seen:
            raise GenError("line %d: duplicate id %r" % (line, e["id"]))
        seen.add(e["id"])
        if e["lead"] != 0x9B + numbers[0]:
            raise GenError("line %d: lead %#x does not match id %r" % (line, e["lead"], e["id"]))
        if e["stack"] not in STACK_OPS:
            raise GenError("line %d: unknown stack %r" % (line, e["stack"]))
        if e["role"] not in ROLES:
            raise GenError("line %d: unknown role %r; add it to ROLES and mud::Role" % (line, e["role"]))
        padded = numbers + [0] * (3 - len(numbers))
        rows.append('    CodeInfo { .mNumbers = { %d, %d, %d }, .mCount = %d, .mId = "%s", .mOp = StackOp::%s, '
                    '.mFg = %d, .mBg = %d, .mParamCount = %d, .mRole = Role::%s },'
                    % (padded[0], padded[1], padded[2], len(numbers), e["id"], STACK_OPS[e["stack"]],
                       e["fg"], e["bg"], len(e["params"]), camel(e["role"])))
    text = "\n".join([
        "// Generated by scripts/gen-c1-codes.py from docs/spec/c1-codes.toml. Do not edit by hand.",
        "#include \"muckaii/mud/codes.hpp\"",
        "",
        "namespace muckaii::mud",
        "{",
        "namespace",
        "{",
        "constexpr std::array<CodeInfo, %d> kTable {" % len(rows),
    ] + rows + [
        "};",
        "} // namespace",
        "",
        "std::span<const CodeInfo> getCodeTable() noexcept",
        "{",
        "    return kTable;",
        "}",
        "} // namespace muckaii::mud",
        "",
    ])
    exe = llvmtools.find("clang-format")
    p = subprocess.run([exe, "--style=file", "--assume-filename=" + OUT], input=text.encode("ascii"),
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=repofiles.ROOT)
    if p.returncode != 0:
        raise GenError("clang-format failed: " + p.stderr.decode("ascii", "replace"))
    return p.stdout


def main(argv):
    check = argv == ["--check"]
    if argv and not check:
        print(__doc__)
        return 2
    try:
        data = generate()
    except (GenError, llvmtools.ToolError, KeyError) as e:
        print("FAIL: " + str(e))
        return 1
    if check:
        current = open(OUT, "rb").read() if os.path.isfile(OUT) else b""
        if current != data:
            print("FAIL: %s is stale; run python scripts/gen-c1-codes.py" % OUT_REL)
            return 1
        print("OK: %s matches docs/spec/c1-codes.toml" % OUT_REL)
        return 0
    with open(OUT, "wb") as f:
        f.write(data)
    print("wrote %s" % OUT_REL)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
