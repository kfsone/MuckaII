#!/usr/bin/env python3
"""clang-tidy (.clang-tidy) finds no violation in MuckaII's own sources.

Usage: check-tidy.py

Runs clang-tidy, at the major version docs/build.md pins (scripts/llvmtools.py), over every .cpp
under src/ except the *Tests.cpp files (GoogleTest's macros do not follow the naming rules). The
compile flags are given directly after `--`, so no build directory is needed: each library's
include directory is src/<layer>/include. On Windows the MSVC environment comes from the Visual
Studio 18 (2026) install's vcvars64.bat, located with vswhere. If clang-tidy cannot be found the
check FAILS with a message saying where it looked.
"""

import glob
import os
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import llvmtools  # noqa: E402
import repofiles  # noqa: E402

FLAGS = ["-std=c++20", "-fno-exceptions", "-D_HAS_EXCEPTIONS=0", "-Wno-unknown-pragmas"]


def vcvars():
    found = []
    for root in llvmtools.vswhere_roots():
        path = os.path.join(root, "VC", "Auxiliary", "Build", "vcvars64.bat")
        if os.path.isfile(path):
            found.append(path)
    return found[0] if found else None


def msvc_environment():
    """The environment vcvars64.bat sets up (once; it takes seconds), or None off Windows."""
    bat = vcvars() if os.name == "nt" else None
    if not bat:
        return None
    out = subprocess.run('"%s" >nul 2>&1 && set' % bat, shell=True,
                         stdout=subprocess.PIPE).stdout
    env = dict(os.environ)
    for line in out.decode("ascii", "replace").splitlines():
        name, sep, value = line.partition("=")
        if sep and name:
            env[name] = value
    return env


def run(cmd, env):
    return subprocess.run(cmd, cwd=repofiles.ROOT, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)


def main(argv):
    if argv:
        print(__doc__)
        return 2
    try:
        exe = llvmtools.find("clang-tidy")
    except llvmtools.ToolError as e:
        print("FAIL: " + str(e))
        return 1
    sources = [p for p in repofiles.files()
               if p.startswith("src/") and p.endswith(".cpp") and not p.endswith("Tests.cpp")]
    if not sources:
        print("FAIL: no sources found under src/, so nothing was checked")
        return 1
    includes = ["-I" + d.replace("\\", "/") for d in sorted(glob.glob(
        os.path.join(repofiles.ROOT, "src", "*", "include")))]
    failed = []
    env = msvc_environment()
    for src in sources:
        p = run([exe, "--quiet", src, "--"] + FLAGS + includes, env)
        text = p.stdout.decode("ascii", "replace")
        if p.returncode != 0:
            failed.append(src)
            print("\n".join(line for line in text.splitlines() if line.strip()))
    if failed:
        print("FAIL: clang-tidy violations in %d of %d file(s)" % (len(failed), len(sources)))
        return 1
    print("OK: %d source file(s) pass clang-tidy (%s)" % (len(sources), exe))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
