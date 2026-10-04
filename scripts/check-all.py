#!/usr/bin/env python3
"""Run every repo gate. Exits 1 if any fails; prints one summary line per check.

Usage: check-all.py [-v]

A failing check's output is printed beneath its line, warnings omitted. -v prints every check's
full output, report-only warnings included.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

CHECKS = [
    ("captures", ["check-captures.py"]),
    ("text-files", ["check-text-files.py"]),
    ("tripwire", ["check-tripwire.py"]),
    ("spec-tags", ["check-spec-tags.py"]),
    ("goldens", ["check-goldens.py"]),
    ("goldens-self-test", ["check-goldens.py", "--self-test"]),
    ("format", ["check-format.py"]),
]


def main(argv):
    verbose = "-v" in argv
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    failed = 0
    for name, cmd in CHECKS:
        p = subprocess.run([sys.executable, os.path.join(HERE, cmd[0])] + cmd[1:],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
        out = p.stdout.decode("ascii", "replace").rstrip("\n").split("\n")
        last = out[-1] if out else ""
        ok = p.returncode == 0
        nwarn = sum(1 for line in out if line.startswith("warning:"))
        print("%-4s %-18s %s" % ("PASS" if ok else "FAIL", name, last))
        if not ok or verbose:
            for line in out[:-1]:
                if verbose or not line.startswith("warning:"):
                    print("     | " + line)
            if nwarn and not verbose:
                print("     | (%d warning line(s) hidden; -v shows them)" % nwarn)
        if not ok:
            failed += 1
    print("check-all: %d of %d check(s) failed" % (failed, len(CHECKS)) if failed
          else "check-all: all %d check(s) passed" % len(CHECKS))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
