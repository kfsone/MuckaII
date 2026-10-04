#!/usr/bin/env python3
"""Every tracked C++ file is formatted exactly as clang-format (.clang-format) would write it.

Usage: check-format.py

Runs `clang-format --dry-run --Werror` over every .cpp .hpp .h .cc .cxx .hxx file that git lists
(scripts/repofiles.py). The tool is clang-format at the major version docs/build.md pins
(scripts/llvmtools.py). If it cannot be found the check FAILS with a message saying where it
looked: a style gate that skips itself when its tool is missing is not a gate.
To fix a violation locally: clang-format -i <file>.
"""

import os
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import llvmtools  # noqa: E402
import repofiles  # noqa: E402

CXX_EXTENSIONS = (".cpp", ".hpp", ".h", ".cc", ".cxx", ".hxx")
BATCH = 40


def main(argv):
    if argv:
        print(__doc__)
        return 2
    try:
        exe = llvmtools.find("clang-format")
    except llvmtools.ToolError as e:
        print("FAIL: " + str(e))
        return 1
    sources = [p for p in repofiles.files() if p.lower().endswith(CXX_EXTENSIONS)]
    if not sources:
        print("FAIL: no C++ files found, so nothing was checked")
        return 1
    failed = False
    for i in range(0, len(sources), BATCH):
        batch = sources[i:i + BATCH]
        p = subprocess.run([exe, "--dry-run", "--Werror", "--style=file"] + batch, cwd=repofiles.ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if p.returncode != 0:
            failed = True
            lines = p.stdout.decode("ascii", "replace").splitlines()
            for line in lines:
                if ": error:" in line or ": warning:" in line:
                    print(line)
            if not any(": error:" in line for line in lines):
                print("\n".join(lines))
    if failed:
        print("FAIL: formatting violations; run clang-format -i on the files named above")
        return 1
    print("OK: %d C++ file(s) formatted (%s)" % (len(sources), exe))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
