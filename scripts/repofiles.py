"""The repo's file list, shared by the tree-walking checks.

Lists tracked files plus untracked files that are not ignored (`git ls-files --cached --others
--exclude-standard`), so work not yet committed is checked too. Paths are repo-relative with
forward slashes; files listed by git but absent on disk are skipped.
"""

import os
import subprocess

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def files():
    out = subprocess.check_output(
        ["git", "-C", ROOT, "ls-files", "-z", "--cached", "--others", "--exclude-standard"])
    seen = set()
    result = []
    for p in out.split(b"\0"):
        if not p:
            continue
        p = p.decode("utf-8", "surrogateescape")
        if p in seen or not os.path.isfile(os.path.join(ROOT, p)):
            continue
        seen.add(p)
        result.append(p)
    return sorted(result)


def read(path):
    with open(os.path.join(ROOT, path), "rb") as f:
        return f.read()
